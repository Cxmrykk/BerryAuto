#pragma once
#include "input.pb.h"

void init_uinput();
void reset_input_state();
void handle_touch_event(const com::andrerinas::headunitrevived::aap::protocol::proto::InputReport& report);
void cleanup_input();
void wake_up_display();
