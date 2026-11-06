#pragma once
#include "usb_cam/protocol.h"
namespace button_input {
bool begin();
void write_header(usb_cam::Header& header);
void acknowledge(usb_cam::ButtonAck ack);
}
