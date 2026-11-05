#pragma once
#include <cstddef>
#include <cstdint>

namespace camera_link {
bool begin();
bool ready();
bool exchange(const uint8_t* header, const uint8_t* jpeg, size_t length, uint8_t* control);
}  // namespace camera_link
