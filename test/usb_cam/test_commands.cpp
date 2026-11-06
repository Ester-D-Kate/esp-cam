#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "usb_cam/commands.h"
using namespace usb_cam;
int main() {
  Config c;
  assert(apply_command("set brightness 1", c) == CommandResult::Changed);
  assert(c.generation == 2 && c.settings[0] == 1);
  assert(apply_command("set brightness 3", c) == CommandResult::Invalid);
  assert(c.generation == 2 && c.settings[0] == 1);
  assert(apply_command("set quality 12junk", c) == CommandResult::Invalid);
  assert(apply_command("set quality 9999999999999999999999999", c) == CommandResult::Invalid);
  assert(apply_command("set quality 12", c) == CommandResult::Changed && c.quality == 12);
  assert(apply_command("set xclk_mhz 40", c) == CommandResult::Invalid);
  assert(apply_command("set xclk_mhz 10", c) == CommandResult::Changed && c.xclk_mhz == 10);
  assert(apply_command("set xclk_mhz 24", c) == CommandResult::Changed && c.xclk_mhz == 24);
  assert(apply_command("set exposure_register 55", c) == CommandResult::Invalid);
  assert(apply_command("set brightness default", c) == CommandResult::Changed);
  assert(c.settings[0] == kDefaultSetting);
  assert(apply_command("defaults", c) == CommandResult::Changed && c.xclk_mhz == 20);
  assert(apply_command("status", c) == CommandResult::Status);
  LineReader lines;
  for (unsigned i = 0; i < 200; ++i) assert(!lines.push('x'));
  assert(lines.push('\n') && lines.overflowed());
  for (char ch : {'s','t','a','t','u','s'}) assert(!lines.push(ch));
  assert(lines.push('\n') && !lines.overflowed());
  assert(apply_command(lines.line(), c) == CommandResult::Status);
  c = Config{};
  assert(apply_command("set pll_multiplier 30", c) == CommandResult::Invalid);
  assert(apply_command("set pll_multiplier 30", c, USB_CAM_PROFILE_OV3660_LEGACY) == CommandResult::Invalid);
  assert(apply_command("set pll_multiplier 30", c, USB_CAM_PROFILE_OV3660) == CommandResult::Changed);
  assert(c.pll.multiplier == 30);
  auto before = c;
  assert(apply_command("set pll_multiplier 15", c, USB_CAM_PROFILE_OV3660) == CommandResult::Invalid);
  assert(config_equal(c, before));
  assert(apply_command("set pclk_div 31", c, USB_CAM_PROFILE_OV3660) == CommandResult::Invalid);
  assert(apply_command("set pclk_div 12", c, USB_CAM_PROFILE_OV3660) == CommandResult::Changed);
  assert(apply_command("set pclk_div 2", c, USB_CAM_PROFILE_OV3660) == CommandResult::Invalid);
  assert(apply_command("set pll_pre_div 4", c, USB_CAM_PROFILE_OV3660) == CommandResult::Invalid);
  assert(apply_command("set pll_multiplier 0", c, USB_CAM_PROFILE_OV3660) == CommandResult::Changed);
  assert(!c.pll.multiplier);
  assert(apply_command("set pll_root_2x 1", c, USB_CAM_PROFILE_OV3660) == CommandResult::Invalid);
  c = mode_config(10, USB_CAM_INTERVAL(5), USB_CAM_PROFILE_OV3660);
  c.quality = 50;
  assert(apply_command("defaults", c, USB_CAM_PROFILE_OV3660) == CommandResult::Changed);
  assert(c.width == 1920 && c.quality == 28 && c.pll.multiplier == 16 && c.pll.pre_div == 1);
  const auto full=USB_CAM_PROFILE_OV3660_FULL;
  c=mode_config(4,USB_CAM_INTERVAL(120),full);
  assert(apply_command("set readout_total_y 200",c,full)==CommandResult::Invalid);
  assert(apply_command("set readout_total_y 300",c,USB_CAM_PROFILE_OV3660)==CommandResult::Invalid);
  assert(apply_command("set readout_total_y 300",c,full)==CommandResult::Changed);
  assert(c.readout.total_y==300);
  assert(apply_command("defaults",c,full)==CommandResult::Changed);
  assert(c.width==320 && c.interval_100ns==USB_CAM_INTERVAL(120) && c.readout.total_y==278);
  assert(apply_command("set readout_enabled 0",c,full)==CommandResult::Changed);
  assert(apply_command("set readout_start_x 385",c,full)==CommandResult::Changed);
  before=c;
  assert(apply_command("set readout_enabled 1",c,full)==CommandResult::Invalid && config_equal(c,before));
  assert(apply_command("set readout_start_x 384",c,full)==CommandResult::Changed);
  assert(apply_command("set readout_enabled 1",c,full)==CommandResult::Changed);
  assert(apply_command("set readout_x_increment 33",c,full)==CommandResult::Invalid);
  assert(apply_command("set readout_binning 2",c,full)==CommandResult::Invalid);
  assert(apply_command("set readout_total_x -1",c,full)==CommandResult::Invalid);
  Config selection; bool reset=false;
  assert(select_mode(11,USB_CAM_INTERVAL(5),full,c,false,true,selection,reset));
  assert(reset && selection.width==2048 && !selection.readout.enabled);
  c=mode_config(1,USB_CAM_INTERVAL(30),full);
  c.settings[unsigned(Setting::Aec2)]=1;
  before=selection;
  assert(!select_mode(1,USB_CAM_INTERVAL(60),full,c,false,false,selection,reset));
  assert(config_equal(selection,before)); // Failed COMMIT cannot publish partial settings.
  puts("Command tests passed: limits, readout staging, defaults, atomic validation and overflow recovery.");
}
