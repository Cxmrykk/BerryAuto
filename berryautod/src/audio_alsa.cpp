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
    // Connect to the Daemon-facing side of the BerryAuto virtual soundcard (Device 1)
    if ((err = snd_pcm_open(&pcm_playback_handle, "plughw:BerryAutoAudio,1,0", SND_PCM_STREAM_PLAYBACK, 0)) < 0)
    {
        LOG_E("[ALSA] Cannot open BerryAutoAudio for playback (mic): " << snd_strerror(err));
        return false;
    }

    // Set to the exact Android Auto constraints hardcoded in our DKMS module
    if ((err = snd_pcm_set_params(pcm_playback_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, 16000,
                                  1, 500000)) < 0)
    {
        LOG_E("[ALSA] Cannot set params for playback (mic): " << snd_strerror(err));
        return false;
    }
    if ((err = snd_pcm_prepare(pcm_playback_handle)) < 0)
    {
        LOG_E("[ALSA] Cannot prepare playback stream: " << snd_strerror(err));
        return false;
    }
    return true;
}

bool init_alsa_capture()
{
    int err;
    // Connect to the Daemon-facing side of the BerryAuto virtual soundcard (Device 1)
    if ((err = snd_pcm_open(&pcm_capture_handle, "plughw:BerryAutoAudio,1,0", SND_PCM_STREAM_CAPTURE, 0)) < 0)
    {
        LOG_E("[ALSA] Cannot open BerryAutoAudio for capture (media): " << snd_strerror(err));
        return false;
    }

    // Set to the exact Android Auto constraints hardcoded in our DKMS module
    if ((err = snd_pcm_set_params(pcm_capture_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2, 48000, 1,
                                  500000)) < 0)
    {
        LOG_E("[ALSA] Cannot set params for capture (media): " << snd_strerror(err));
        return false;
    }
    if ((err = snd_pcm_prepare(pcm_capture_handle)) < 0)
    {
        LOG_E("[ALSA] Cannot prepare capture stream: " << snd_strerror(err));
        return false;
    }
    return true;
}

void audio_capture_loop()
{
    const int frames = 1024;
    int buffer_size = frames * 4; // 2 channels * 2 bytes (16-bit)
    std::vector<uint8_t> buffer(buffer_size);

    uint64_t last_log_time = get_monotonic_usec();
    uint64_t sample_index = 0; // Tracks position in the sine wave

    while (!should_exit.load())
    {
        // Wait until the car grants us audio focus and the channel is open
        if (!is_audio_streaming.load() || !has_audio_focus.load() || audio_channel_id < 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // ---------------------------------------------------------
        // BYPASS ALSA ENTIRELY: Generate a 440Hz Beep mathematically
        // ---------------------------------------------------------
        int16_t* pcm_samples = reinterpret_cast<int16_t*>(buffer.data());
        for (int i = 0; i < frames; i++) {
            // Generate 440Hz sine wave, at 48000Hz sample rate, 50% volume (16000/32767)
            int16_t wave = (int16_t)(std::sin(2.0 * M_PI * 440.0 * (sample_index / 48000.0)) * 16000.0);
            
            // Stereo: Left and Right channel get the same wave
            pcm_samples[i * 2]     = wave; // Left
            pcm_samples[i * 2 + 1] = wave; // Right
            
            sample_index++;
        }
        // ---------------------------------------------------------

        // Prepare the Android Auto packet
        std::vector<uint8_t> pt;
        pt.reserve(buffer_size + 10);

        // 2-byte AAP type header (0x00, 0x00 = Media Data)
        pt.push_back(0x00);
        pt.push_back(0x00);

        // 8-byte AAP timestamp (Big Endian)
        uint64_t timestamp = get_monotonic_usec();
        for (int i = 7; i >= 0; --i)
        {
            pt.push_back((timestamp >> (i * 8)) & 0xFF);
        }

        pt.insert(pt.end(), buffer.data(), buffer.data() + buffer_size);

        // Send over USB
        send_media_payload(audio_channel_id, pt);

        if (timestamp - last_log_time > 1000000ULL)
        {
            LOG_I("[DEBUG] 1s elapsed. Injected raw 440Hz Sine Wave directly to car!");
            last_log_time = timestamp;
        }

        // Throttle the loop to exactly 48kHz (1024 frames = ~21.3 milliseconds)
        // Since we bypassed ALSA's hardware blocking, we must manually sleep.
        std::this_thread::sleep_for(std::chrono::milliseconds(21));
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

    int frames = len / 2; // 1 channel, 16-bit
    int err = snd_pcm_writei(pcm_playback_handle, data, frames);
    if (err < 0)
    {
        int rec_err = snd_pcm_recover(pcm_playback_handle, err, 0);
        if (rec_err < 0)
        {
            snd_pcm_prepare(pcm_playback_handle);
        }
        else
        {
            snd_pcm_writei(pcm_playback_handle, data, frames); // Retry write
        }
    }

    static uint64_t mic_frames = 0;
    static uint64_t last_mic_log = get_monotonic_usec();
    mic_frames++;
    if (get_monotonic_usec() - last_mic_log > 5000000ULL)
    {
        LOG_I("[ALSA] Mic receiving is ACTIVE. Injected " << mic_frames << " packets into the OS.");
        last_mic_log = get_monotonic_usec();
    }
}
