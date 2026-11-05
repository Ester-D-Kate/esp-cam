#pragma once

// Adapted from TinyUSB's RP2040 endpoint abort sequence. See THIRD_PARTY_NOTICES.md.
// Included after the RP2040 driver types (or the host test's register model).
// The caller holds interrupts disabled through the subsequent activation.
static bool usb_cam_abort_iso(struct hw_endpoint* ep, uint32_t mask) {
  if (!ep->active && !ep->pending && !(*ep->buffer_control & USB_BUF_CTRL_AVAIL)) return true;
  // RP2040-E2: ABORT cannot safely cancel an in-flight transfer on B0/B1.
  if (rp2040_chip_version() < 2) return false;

  usb_hw_set->abort = mask;
  const uint32_t started = time_us_32();
  while ((usb_hw->abort_done & mask) != mask) {
    // Leave the buffer owned by hardware on failure. The caller must reset the
    // entire controller before permitting another transfer.
    if ((uint32_t)(time_us_32() - started) >= 1000) return false;
  }
  *ep->buffer_control = 0;
  usb_hw_clear->buf_status = mask;
  hw_endpoint_reset_transfer(ep);
  ep->pending = 0;
  ep->next_pid = 0;
  usb_hw_clear->abort_done = mask;
  usb_hw_clear->abort = mask;
  return true;
}

static bool usb_cam_reopen_endpoint(struct hw_endpoint* ep, uint32_t mask) {
  if (!usb_cam_abort_iso(ep, mask)) return false;
  hw_endpoint_enable(ep);
  // TinyUSB 0.18's usbd_edpt_open does not clear its busy/claimed flags.
  // Hardware is now quiescent, so reset those flags and the DATA0 PID together.
  usbd_edpt_clear_stall(0, ep->ep_addr);
  // clear_stall clears busy but not claimed in TinyUSB 0.18. A cancelled
  // transfer has no completion event to release the claim for us.
  (void)usbd_edpt_release(0, ep->ep_addr);
  return true;
}
