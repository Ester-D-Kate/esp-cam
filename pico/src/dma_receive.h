#pragma once

#include <stddef.h>
#include <stdint.h>

// Included after the SDK DMA API, or the host test's register model.
static inline size_t usb_cam_finish_dma(unsigned channel, const void* buffer, size_t capacity) {
  dma_channel_abort(channel);
  // RP2040 CHAN_ABORT clears TRANS_COUNT. WRITE_ADDR retains the position after
  // outstanding writes have been flushed, so use that to count received bytes.
  // This channel increments its destination once for each 8-bit transfer.
  const uintptr_t begin = (uintptr_t)buffer;
  const uintptr_t end = dma_channel_hw_addr(channel)->write_addr;
  if (end < begin || end - begin > capacity) return capacity + 1;
  return (size_t)(end - begin);
}
