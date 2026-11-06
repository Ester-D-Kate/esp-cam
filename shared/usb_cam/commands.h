#pragma once
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include "configuration.h"
namespace usb_cam {
enum class CommandResult { Invalid, Changed, Status };
inline CommandResult apply_command(const char* line, Config& current, UsbCamProfile profile = USB_CAM_PROFILE_OV2640) {
  if (!line) return CommandResult::Invalid;
  if (strcmp(line, "status") == 0) return CommandResult::Status;
  Config next = current;
  if (strcmp(line, "defaults") == 0) {
    bool found=false;
    for (unsigned i = 1; i <= usb_cam_profile_count(profile); ++i) {
      const auto* mode = usb_cam_profile_mode(profile, i);
      if (mode->width == current.width && mode->height == current.height) {
        next=mode_config(i,current.interval_100ns,profile); found=true; break;
      }
    }
    if (!found) return CommandResult::Invalid;
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
      if ((profile != USB_CAM_PROFILE_OV3660 && profile != USB_CAM_PROFILE_OV3660_FULL) ||
          use_default || value < 0 || value > 31) return CommandResult::Invalid;
      if (named("pll_multiplier")) next.pll.multiplier = uint8_t(value);
      else if (named("pll_sys_div")) next.pll.sys_div = uint8_t(value);
      else if (named("pll_pre_div")) next.pll.pre_div = uint8_t(value);
      else if (named("pll_root_2x")) next.pll.root_2x = uint8_t(value);
      else if (named("pll_seld5")) next.pll.seld5 = uint8_t(value);
      else next.pll.pclk_div = uint8_t(value);
    } else if (size > 8 && memcmp(name,"readout_",8)==0) {
      if (profile!=USB_CAM_PROFILE_OV3660_FULL || use_default || value<0 || value>4095)
        return CommandResult::Invalid;
      if (named("readout_enabled") || named("readout_binning") || named("readout_scale")) {
        if (value>1) return CommandResult::Invalid;
        if (named("readout_enabled")) next.readout.enabled=value;
        else if (named("readout_binning")) next.readout.binning=value;
        else next.readout.scale=value;
      } else if (named("readout_x_increment") || named("readout_y_increment")) {
        if (value!=0x11 && value!=0x31 && value!=0x71) return CommandResult::Invalid;
        if (named("readout_x_increment")) next.readout.x_increment=uint8_t(value);
        else next.readout.y_increment=uint8_t(value);
      } else {
        bool found=false;
#define USB_CAM_SET_READOUT(field) if (named("readout_" #field)) { next.readout.field=uint16_t(value); found=true; }
        USB_CAM_READOUT_FIELDS(USB_CAM_SET_READOUT)
#undef USB_CAM_SET_READOUT
        if (!found) return CommandResult::Invalid;
      }
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
// Merge host selection with explicit controls before publishing any new state.
// A crop override that cannot fit a new resolution falls back to its preset.
inline bool select_mode(unsigned index, uint32_t interval, UsbCamProfile profile,
    const Config& current, bool custom_quality, bool custom_readout,
    Config& out, bool& readout_reset) {
  Config next=mode_config(index,interval,profile);
  next.generation=current.generation;
  next.xclk_mhz=current.xclk_mhz; next.pll=current.pll;
  memcpy(next.settings,current.settings,sizeof(next.settings));
  if (custom_quality) next.quality=current.quality;
  bool reset=false;
  if (custom_readout) {
    const Readout preset=next.readout;
    next.readout=current.readout;
    if (!valid_readout(next)) { next.readout=preset; reset=true; }
  }
  if (!valid_config(next)) return false;
  out=next; readout_reset=reset;
  return true;
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
