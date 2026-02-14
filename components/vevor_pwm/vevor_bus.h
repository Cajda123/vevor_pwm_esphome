#pragma once
#include "esphome.h"

void vevor_init();
void vevor_send_byte(uint8_t b);
void vevor_set_receive_callback(std::function<void(uint8_t, uint16_t)> cb);
