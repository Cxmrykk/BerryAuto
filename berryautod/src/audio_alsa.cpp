#include "audio_alsa.hpp"
#include "aap_sender.hpp"
#include "globals.hpp"
#include <alsa/asoundlib.h>
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
    if ((err = snd_pcm_open(&pcm_playback_handle, "plughw:Loopback,1,1", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    {
        LOG_E("[ALSA] Cannot open Loopback for playback (mic): " << snd_strerror(err));
        return false;
    }
    if ((err = snd_pcm_set_params(pcm_playback_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, 16000,
                                  1, 500000)) < 0)
    {
        LOG_E("[ALSA] Cannot set params for playback (mic): " << snd_strerror(err));
        return false;
    }
    return true;
}

bool init_alsa_capture()
{
    int err;
    if ((err = snd_pcm_open(&pcm_capture_handle, "plughw:Loopback,1,0", SND_PCM_STREAM_CAPTURE, 0)) < 0)
    {
        LOG_E("[ALSA] Cannot open Loopback for capture (audio): " << snd_strerror(err));
        return false;
    }
    if ((err = snd_pcm_set_params(pcm_capture_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1,
                                  500000)) < 0)
    {
        LOG_E("[ALSA] Cannot set params for capture (audio): " << snd_strerror(err));
        return false;
    }
    return true;
}

void audio_capture_loop()
{
    const int frames = 1024;
    int buffer_size = frames * 4; // 2 channels * 2 bytes
    std::vector<uint8_t> buffer(buffer_size);

    uint64_t last_log_time = get_monotonic_usec();
    uint64_t frames_sent = 0;

    while (!should_exit.load())
    {
        // If the car hasn't started the stream or granted focus, wait.
        if (!is_audio_streaming.load() || !has_audio_focus.load() || audio_channel_id < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // This will block until the Linux desktop (PulseAudio/PipeWire) actually plays audio to the loopback!
        int err = snd_pcm_readi(pcm_capture_handle, buffer.data(), frames);

        if (err == -EPIPE)
        {
            snd_pcm_prepare(pcm_capture_handle); // Recover from overrun
            continue;
        }
        else if (err < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (err > 0)
        {
            int bytes_read = err * 4;
            std::vector<uint8_t> pt;
            pt.reserve(bytes_read + 10);

            // 2-byte AAP type header (0x00, 0x00 = Media Data)
            pt.push_back(0x00);
            pt.push_back(0x00);

            // 8-byte AAP timestamp (Big Endian)
            uint64_t timestamp = get_monotonic_usec();
            for (int i = 7; i >= 0; --i)
            {
                pt.push_back((timestamp >> (i * 8)) & 0xFF);
            }

            // Raw PCM payload
            pt.insert(pt.end(), buffer.data(), buffer.data() + bytes_read);

            // Send payload to the car
            send_media_payload(audio_channel_id, pt);
            frames_sent++;

            // Telemetry: Log every 5 seconds to prove the pipeline is active
            if (timestamp - last_log_time > 5000000ULL)
            {
                LOG_I("[ALSA] Audio streaming is ACTIVE. Pushed " << frames_sent << " packets to the car.");
                last_log_time = timestamp;
            }
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
    LOG_I("[ALSA] Audio capture and playback loops initialized successfully.");
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

    int frames = len / 2; // 1 channel, 16-bit
    int err = snd_pcm_writei(pcm_playback_handle, data, frames);
    if (err == -EPIPE)
    {
        snd_pcm_prepare(pcm_playback_handle); // Recover from underrun
        snd_pcm_writei(pcm_playback_handle, data, frames);
    }
    else if (err < 0)
    {
        LOG_E("[ALSA] Mic Write error: " << snd_strerror(err));
    }

    // Telemetry log for incoming mic data
    static uint64_t mic_frames = 0;
    static uint64_t last_mic_log = get_monotonic_usec();
    mic_frames++;
    if (get_monotonic_usec() - last_mic_log > 5000000ULL)
    {
        LOG_I("[ALSA] Mic receiving is ACTIVE. Injected " << mic_frames << " packets into the OS.");
        last_mic_log = get_monotonic_usec();
    }
}
