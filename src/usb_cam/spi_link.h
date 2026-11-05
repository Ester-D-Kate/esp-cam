#pragma once
#include <cstddef>
#include <cstdint>

namespace camera_link {
bool begin();
bool ready();
bool send(const uint8_t* header, const uint8_t* jpeg, size_t length);
}  // namespace camera_link
