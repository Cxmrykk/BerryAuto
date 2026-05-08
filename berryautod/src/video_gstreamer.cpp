#include "globals.hpp"
#include "input_handler.hpp"
#include "video_encoder.hpp"
#include <gst/gst.h>
#include <iostream>
#include <string>
#include <unistd.h>

extern uint64_t get_monotonic_usec();

static std::atomic<bool> first_frame_received{false};

static GstFlowReturn on_new_sample_callback(GstElement* sink, gpointer user_data)
{
    VideoEncoder* enc = static_cast<VideoEncoder*>(user_data);
    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample)
        return GST_FLOW_ERROR;

    if (!first_frame_received.exchange(true))
    {
        LOG_I("[GStreamer] SUCCESS! First frame received from GNOME Portal.");
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps* caps = gst_sample_get_caps(sample);
    if (!buffer || !caps)
    {
        gst_sample_unref(sample);
        return GST_FLOW_ERROR;
    }

    GstStructure* s = gst_caps_get_structure(caps, 0);
    int w = 0, h = 0;
    gst_structure_get_int(s, "width", &w);
    gst_structure_get_int(s, "height", &h);

    GstMapInfo map;
    if (gst_buffer_map(buffer, &map, GST_MAP_READ))
    {
        bool size_changed = false;
        {
            std::lock_guard<std::mutex> lock(enc->frame_mutex);
            if (enc->pw_w != w || enc->pw_h != h)
            {
                enc->pw_w = w;
                enc->pw_h = h;
                enc->pw_fmt = AV_PIX_FMT_BGRA;
                size_changed = true;
            }

            if (enc->latest_frame_buffer.size() != map.size)
                enc->latest_frame_buffer.resize(map.size);

            memcpy(enc->latest_frame_buffer.data(), map.data, map.size);
            enc->latest_stride = w * 4; // BGRA stride is exactly width * 4
        }

        if (size_changed)
        {
            enc->update_sws();
        }

        gst_buffer_unmap(buffer, &map);
    }

    gst_sample_unref(sample);
    return GST_FLOW_OK;
}

bool VideoEncoder::init_gstreamer(uint32_t node_id, int pw_fd)
{
    LOG_I("[GStreamer] Initializing pipeline for Node ID: " << node_id << " (FD: " << pw_fd << ")");

    gst_init(nullptr, nullptr);
    first_frame_received = false;

    // CRITICAL FIXES APPLIED:
    // 1. target-object ensures GStreamer connects correctly to the numeric Node ID assigned by GNOME
    // 2. always-copy=true forces PipeWire to map Pi 4 DRM DMA-BUFs into CPU memory.
    // 3. queue leaky=downstream creates a thread boundary so videoconvert doesn't block the source.
    std::string pipeline_str = "pipewiresrc fd=" + std::to_string(pw_fd) + " target-object=" + std::to_string(node_id) +
                               " always-copy=true keepalive-time=1000 do-timestamp=true " +
                               "! queue max-size-buffers=2 leaky=downstream " + "! videoconvert " +
                               "! video/x-raw,format=BGRA " +
                               "! appsink name=mysink emit-signals=true sync=false drop=true max-buffers=2 async=false";

    GError* err = nullptr;
    pipeline = gst_parse_launch(pipeline_str.c_str(), &err);

    if (err)
    {
        LOG_E("[GStreamer] Pipeline parsing failed: " << err->message);
        g_error_free(err);
        return false;
    }

    appsink = gst_bin_get_by_name(GST_BIN(pipeline), "mysink");
    if (!appsink)
    {
        LOG_E("[GStreamer] Could not find appsink in pipeline!");
        return false;
    }

    g_signal_connect(appsink, "new-sample", G_CALLBACK(on_new_sample_callback), this);

    LOG_I("[GStreamer] Setting pipeline to PLAYING state...");
    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE)
    {
        LOG_E("[GStreamer] Failed to set pipeline to PLAYING state.");
        return false;
    }

    LOG_I("[GStreamer] Pipeline started and negotiating successfully!");
    return true;
}

void VideoEncoder::cleanup_gstreamer()
{
    if (pipeline)
    {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        pipeline = nullptr;
    }
    if (appsink)
    {
        gst_object_unref(appsink);
        appsink = nullptr;
    }
}

void VideoEncoder::run_gstreamer_loop(uint32_t node_id, int pw_fd)
{
    if (!init_gstreamer(node_id, pw_fd))
    {
        cleanup_gstreamer();
        return;
    }

    uint64_t frame_interval_us = 1000000 / target_fps;
    uint64_t next_frame_time = get_monotonic_usec() + frame_interval_us;
    int wake_timer = 0;

    wake_up_display();

    while (running.load())
    {
        wake_timer++;
        if (wake_timer >= target_fps)
        {
            wake_up_display();
            wake_timer = 0;
        }

        GstBus* bus = gst_element_get_bus(pipeline);
        if (bus)
        {
            GstMessage* msg = gst_bus_pop(bus);
            while (msg != nullptr)
            {
                if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR)
                {
                    GError* err = nullptr;
                    gchar* debug_info = nullptr;
                    gst_message_parse_error(msg, &err, &debug_info);
                    LOG_E("[GStreamer] PIPELINE ERROR: " << err->message);
                    if (debug_info)
                        LOG_E("[GStreamer] DEBUG INFO: " << debug_info);
                    g_error_free(err);
                    g_free(debug_info);
                }
                gst_message_unref(msg);
                msg = gst_bus_pop(bus);
            }
            gst_object_unref(bus);
        }

        std::vector<uint8_t> frame_copy;
        int current_stride = 0;
        int current_w = 0;
        int current_h = 0;

        {
            std::lock_guard<std::mutex> lock(frame_mutex);
            current_w = pw_w;
            current_h = pw_h;
            frame_copy = latest_frame_buffer;
            current_stride = latest_stride;
        }

        if (!frame_copy.empty() && current_w > 0 && current_h > 0)
        {
            process_raw_frame(frame_copy.data(), current_stride, current_w, current_h);
        }

        uint64_t now = get_monotonic_usec();
        if (now < next_frame_time)
        {
            usleep(next_frame_time - now);
            next_frame_time += frame_interval_us;
        }
        else
        {
            next_frame_time = now + frame_interval_us;
        }
    }

    cleanup_gstreamer();
}
