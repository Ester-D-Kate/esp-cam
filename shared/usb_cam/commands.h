#pragma once
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "configuration.h"
namespace usb_cam {
enum class CommandResult { Invalid, Changed, Status };
inline CommandResult apply_command(const char* line, Config& current) {
  if (!line) return CommandResult::Invalid;
  if (strcmp(line, "status") == 0) return CommandResult::Status;
  Config next = current;
  if (strcmp(line, "defaults") == 0) {
    next = Config{};
    next.width = current.width; next.height = current.height;
    next.interval_100ns = current.interval_100ns; next.max_jpeg = current.max_jpeg;
    for (unsigned i = 1; i <= USB_CAM_MODE_COUNT; ++i) {
      const auto* mode = usb_cam_mode(i);
      if (mode->width == current.width && mode->height == current.height) next.quality = mode->quality;
    }
  } else {
    if (strncmp(line, "set ", 4) != 0) return CommandResult::Invalid;
    const char* name = line + 4;
    const char* separator = strchr(name, ' ');
    if (!separator || separator == name) return CommandResult::Invalid;
    const size_t size = size_t(separator - name);
    auto named = [name, size](const char* key) { return strlen(key) == size && memcmp(name, key, size) == 0; };
    const char* argument = separator + 1;
    const bool use_default = strcmp(argument, "default") == 0;
    char* end = nullptr;
    errno = 0;
    const long value = use_default ? kDefaultSetting : strtol(argument, &end, 10);
    if (!use_default && (errno || end == argument || *end || value < -32767 || value > 32767))
      return CommandResult::Invalid;
    if (named("quality")) {
      if (use_default || value < 0 || value > 50) return CommandResult::Invalid;
      next.quality = uint8_t(value);
    } else if (named("xclk_mhz")) {
      if (use_default || !supported_xclk(int(value))) return CommandResult::Invalid;
      next.xclk_mhz = uint8_t(value);
    } else if (named("pll_multiplier") || named("pll_sys_div") || named("pll_pre_div") ||
               named("pll_root_2x") || named("pll_seld5") || named("pclk_div")) {
      if (use_default || value < 0 || value > 31) return CommandResult::Invalid;
      if (named("pll_multiplier")) next.pll.multiplier = uint8_t(value);
      else if (named("pll_sys_div")) next.pll.sys_div = uint8_t(value);
      else if (named("pll_pre_div")) next.pll.pre_div = uint8_t(value);
      else if (named("pll_root_2x")) next.pll.root_2x = uint8_t(value);
      else if (named("pll_seld5")) next.pll.seld5 = uint8_t(value);
      else next.pll.pclk_div = uint8_t(value);
    } else {
      bool found = false;
      for (unsigned i = 0; i < kSettingCount; ++i) {
        if (!named(kSettingSpecs[i].name)) continue;
        next.settings[i] = int16_t(value); found = true; break;
      }
      if (!found) return CommandResult::Invalid;
    }
  }
  if (!valid_config(next)) return CommandResult::Invalid;
  next.generation = current.generation + 1;
  current = next;
  return CommandResult::Changed;
}
class LineReader {
 public:
  bool push(char ch) {
    if (finished_) { size_ = 0; overflow_ = false; finished_ = false; }
    if (ch == '\r') return false;
    if (ch == '\n') { buffer_[size_] = 0; finished_ = true; return true; }
    if (ch == 0 || size_ + 1 >= sizeof(buffer_)) overflow_ = true;
    else if (!overflow_) buffer_[size_++] = ch;
    return false;
  }
  bool overflowed() const { return overflow_; }
  const char* line() const { return buffer_; }
 private:
  char buffer_[80]{};
  size_t size_ = 0;
  bool overflow_ = false, finished_ = false;
};
} // namespace usb_cam
