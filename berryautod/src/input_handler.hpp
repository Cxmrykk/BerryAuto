#pragma once
#include "input.pb.h"

void handle_touch_event(const com::andrerinas::headunitrevived::aap::protocol::proto::InputReport& report);
void cleanup_input();
