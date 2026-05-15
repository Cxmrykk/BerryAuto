#include "audio_alsa.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include <alsa/asoundlib.h>
#include <cmath>
#include <iostream>
#include <thread>
#include <vector>

extern uint64_t get_monotonic_usec();

static snd_pcm_t* pcm_capture_handle = nullptr;
static snd_pcm_t* pcm_playback_handle = nullptr;
static std::thread audio_capture_thread;

bool init_alsa_playback()
{
    int err;
    // We try our custom DKMS module first, but fallback to standard aloop if needed
    const char* device = "plughw:BerryAutoAudio,1,0";
    if (snd_pcm_open(&pcm_playback_handle, device, SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
        device = "plughw:Loopback,1,0"; // Fallback to standard aloop
        if (snd_pcm_open(&pcm_playback_handle, device, SND_PCM_STREAM_PLAYBACK, 0) < 0)
        {
            LOG_E("[ALSA] Cannot open any playback device!");
            return false;
        }
    }

    snd_pcm_set_params(pcm_playback_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, 16000, 1, 500000);
    snd_pcm_prepare(pcm_playback_handle);
    return true;
}

bool init_alsa_capture()
{
    int err;
    const char* device = "plughw:BerryAutoAudio,1,0";
    if (snd_pcm_open(&pcm_capture_handle, device, SND_PCM_STREAM_CAPTURE, 0) < 0)
    {
        device = "plughw:Loopback,1,1"; // Fallback to standard aloop
        if (snd_pcm_open(&pcm_capture_handle, device, SND_PCM_STREAM_CAPTURE, 0) < 0)
        {
            LOG_E("[ALSA] Cannot open any capture device!");
            return false;
        }
    }

    snd_pcm_set_params(pcm_capture_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1, 500000);
    snd_pcm_prepare(pcm_capture_handle);
    return true;
}

void audio_capture_loop()
{
    const int frames = 1024;
    int buffer_size = frames * 4; // 2 channels * 2 bytes
    std::vector<uint8_t> buffer(buffer_size);

    uint64_t last_log_time = get_monotonic_usec();
    uint64_t sample_index = 0;
    int consecutive_silent_chunks = 0;

    while (!should_exit.load())
    {
        if (!is_audio_streaming.load() || !has_audio_focus.load() || audio_channel_id < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        int err = snd_pcm_readi(pcm_capture_handle, buffer.data(), frames);

        // Handle Overruns (-EPIPE) and Hardware errors
        if (err < 0)
        {
            if (err == -EPIPE)
            {
                // Silent XRUN recovery (daemon was slightly too slow processing)
            }
            else if (err == -EIO)
            {
                LOG_E("[ALSA] Fatal -EIO from loopback. Recovering...");
            }
            else if (err != -EAGAIN)
            {
                LOG_E("[ALSA] Capture error: " << snd_strerror(err) << ". Recovering...");
            }

            int rec_err = snd_pcm_recover(pcm_capture_handle, err, 0);
            if (rec_err < 0)
            {
                snd_pcm_prepare(pcm_capture_handle);
            }
            // Loop immediately to catch up. DO NOT SLEEP HERE!
            continue;
        }

        // --- VOLUME ANALYZER ---
        int chunk_peak = 0;
        int16_t* pcm_samples = reinterpret_cast<int16_t*>(buffer.data());
        for (int i = 0; i < (err * 2); i++)
        {
            if (std::abs(pcm_samples[i]) > chunk_peak)
                chunk_peak = std::abs(pcm_samples[i]);
        }

        if (chunk_peak == 0)
        {
            consecutive_silent_chunks++;
        }
        else
        {
            consecutive_silent_chunks = 0; // We heard real audio!
        }

        // Overwrite the pure silence with the diagnostic tone (in-place)
        if (consecutive_silent_chunks > 150)
        {
            for (int i = 0; i < err; i++)
            {
                int16_t wave = (int16_t)(std::sin(2.0 * M_PI * 440.0 * (sample_index / 48000.0)) * 1000.0);
                pcm_samples[i * 2] = wave;
                pcm_samples[i * 2 + 1] = wave;
                sample_index++;
            }
        }

        // Package and send to car
        int bytes_read = err * 4;
        std::vector<uint8_t> pt;
        pt.reserve(bytes_read + 10);

        pt.push_back(0x00);
        pt.push_back(0x00);

        uint64_t timestamp = get_monotonic_usec();
        for (int i = 7; i >= 0; --i)
        {
            pt.push_back((timestamp >> (i * 8)) & 0xFF);
        }

        pt.insert(pt.end(), buffer.data(), buffer.data() + bytes_read);
        send_media_payload(audio_channel_id, pt);

        if (timestamp - last_log_time > 3000000ULL)
        {
            if (consecutive_silent_chunks > 150)
            {
                LOG_I("[ALSA] OS is sending pure silence. Injected diagnostic beep.");
            }
            else
            {
                LOG_I("[ALSA] Real desktop audio is flowing perfectly.");
            }
            last_log_time = timestamp;
        }
    }
}

bool init_alsa()
{
    if (!init_alsa_playback())
        return false;
    if (!init_alsa_capture())
        return false;
    audio_capture_thread = std::thread(audio_capture_loop);
    audio_capture_thread.detach();
    return true;
}

void stop_alsa()
{
    if (audio_capture_thread.joinable())
        audio_capture_thread.join();
    if (pcm_capture_handle)
    {
        snd_pcm_close(pcm_capture_handle);
        pcm_capture_handle = nullptr;
    }
    if (pcm_playback_handle)
    {
        snd_pcm_drain(pcm_playback_handle);
        snd_pcm_close(pcm_playback_handle);
        pcm_playback_handle = nullptr;
    }
}

void inject_mic_data(const uint8_t* data, int len)
{
    if (!pcm_playback_handle)
        return;
    int frames = len / 2;
    int err = snd_pcm_writei(pcm_playback_handle, data, frames);
    if (err < 0)
    {
        if (snd_pcm_recover(pcm_playback_handle, err, 0) >= 0)
        {
            snd_pcm_writei(pcm_playback_handle, data, frames);
        }
    }
}
