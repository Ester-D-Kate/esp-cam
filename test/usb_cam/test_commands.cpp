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
  puts("Command tests passed: limits, atomic validation and overflow recovery.");
}
