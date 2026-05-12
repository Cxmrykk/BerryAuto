#pragma once
#include <stdint.h>

bool init_alsa();
void stop_alsa();
void inject_mic_data(const uint8_t* data, int len);
