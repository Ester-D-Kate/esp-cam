#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct dma_channel_registers { uintptr_t write_addr; uint32_t transfer_count; };
static struct dma_channel_registers registers;
static uintptr_t in_flight_end;
static unsigned aborts;
static void dma_channel_abort(unsigned channel) {
  assert(channel == 2);
  ++aborts;
  registers.transfer_count = 0;  // RP2040 CHAN_ABORT clears this counter.
  registers.write_addr = in_flight_end;  // Abort flushes outstanding bus writes.
}
static struct dma_channel_registers* dma_channel_hw_addr(unsigned channel) {
  assert(channel == 2);
  return &registers;
}
#include "dma_receive.h"

static const uintptr_t base = 0x20001000;
static size_t finish(size_t capacity, size_t arrived, size_t pending) {
  registers.write_addr = base + arrived;
  registers.transfer_count = (uint32_t)(capacity - arrived);
  in_flight_end = registers.write_addr + pending;
  return usb_cam_finish_dma(2, (const void*)base, capacity);
}

int main(void) {
  assert(finish(24625, 12048, 0) == 12048);  // A normal, partly filled frame.
  assert(finish(24625, 0, 0) == 0);
  assert(finish(24625, 12047, 1) == 12048);  // Count a final in-flight write.
  assert(finish(24625, 24625, 0) == 24625);  // Preserve the extra-byte guard.
  registers.write_addr = in_flight_end = base - 1;
  assert(usb_cam_finish_dma(2, (const void*)base, 24625) > 24625);
  assert(aborts == 5);
  puts("DMA receive tests passed: abort counter reset, final write, empty and overlength frames.");
}
