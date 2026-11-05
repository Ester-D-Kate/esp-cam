#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

// MMIO model for the reset helper. The production helper itself is compiled
// unchanged; this model asserts it never clears a buffer before hardware abort.
struct hw_endpoint { bool active; uint8_t pending; uint8_t next_pid; volatile uint32_t* buffer_control; };
struct usb_registers { uint32_t abort, abort_done, buf_status; };
static struct usb_registers registers, set_registers, clear_registers;
static struct usb_registers* const usb_hw = &registers;
static struct usb_registers* const usb_hw_set = &set_registers;
static struct usb_registers* const usb_hw_clear = &clear_registers;
static uint32_t buffer_control;
static unsigned chip_version, ticks;
static bool acknowledge;
#define USB_BUF_CTRL_AVAIL (1u << 10)
static unsigned rp2040_chip_version(void) { return chip_version; }
static uint32_t time_us_32(void) {
  ++ticks;
  if (ticks == 4 && acknowledge) {
    assert(usb_hw_set->abort == 4);
    assert(buffer_control & USB_BUF_CTRL_AVAIL);
    usb_hw->abort_done = 4;
  }
  return ticks * 100;
}
static void hw_endpoint_reset_transfer(struct hw_endpoint* ep) { ep->active = false; }
#include "iso_restart.h"

static struct hw_endpoint setup(unsigned version, bool ack) {
  registers = (struct usb_registers){0};
  set_registers = (struct usb_registers){0};
  clear_registers = (struct usb_registers){0};
  buffer_control = USB_BUF_CTRL_AVAIL | 123;
  chip_version = version;
  ticks = 0;
  acknowledge = ack;
  return (struct hw_endpoint){true, 1, 1, &buffer_control};
}

int main(void) {
  struct hw_endpoint ep = setup(2, true);
  assert(usb_cam_abort_iso(&ep, 4));
  assert(buffer_control == 0 && !ep.active && !ep.pending && ep.next_pid == 0);
  assert(usb_hw_clear->abort == 4 && usb_hw_clear->abort_done == 4 && usb_hw_clear->buf_status == 4);

  ep = setup(2, false);
  assert(!usb_cam_abort_iso(&ep, 4));
  assert(ep.active && (buffer_control & USB_BUF_CTRL_AVAIL));
  assert(ticks < 20);  // A missing hardware acknowledgment must not hang the CPU.

  ep = setup(1, true);
  assert(!usb_cam_abort_iso(&ep, 4));  // RP2040-E2: abort is unsafe on B0/B1.
  assert(ep.active && !usb_hw_set->abort && (buffer_control & USB_BUF_CTRL_AVAIL));

  ep = setup(2, false);
  ep.active = false;
  ep.pending = 0;
  buffer_control = 0;
  assert(usb_cam_abort_iso(&ep, 4));
  assert(!usb_hw_set->abort);
  puts("ISO restart tests passed: pending packet, abort ordering, timeout, RP2040-E2.");
}
