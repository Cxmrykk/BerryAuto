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
    // Mic data coming from AA (Headunit) -> Inject into Loopback so OS can read it as a microphone
    if ((err = snd_pcm_open(&pcm_playback_handle, "hw:Loopback,1,1", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
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
    // Audio from OS Desktop -> Read from Loopback and send to AA (Headunit)
    if ((err = snd_pcm_open(&pcm_capture_handle, "hw:Loopback,1,0", SND_PCM_STREAM_CAPTURE, 0)) < 0)
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

    while (!should_exit.load())
    {
        if (!is_audio_streaming.load() || !has_audio_focus.load() || audio_channel_id < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

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

            // Send standard PCM audio payload to the Audio Channel
            send_media_payload(audio_channel_id, pt);
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
    {
        audio_capture_thread.join();
    }
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
        LOG_E("[ALSA] Write error: " << snd_strerror(err));
    }
}
