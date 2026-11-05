// Compile the pinned SDK driver with a guarded ISO reactivation adapter.
// Keeping this in the project avoids modifying the installed SDK package.
#define dcd_edpt_iso_activate usb_cam_original_iso_activate
#include "portable/raspberrypi/rp2040/dcd_rp2040.c"
#undef dcd_edpt_iso_activate
#include "iso_restart.h"

_Static_assert(TUSB_VERSION_NUMBER == 1800, "Review the RP2040 adapter when upgrading TinyUSB");

static bool recovery_required;

bool usb_cam_usb_recovery_required(void) {
  const bool result = recovery_required;
  recovery_required = false;
  return result;
}

bool dcd_edpt_iso_activate(uint8_t rhport, const tusb_desc_endpoint_t* descriptor) {
  const uint8_t address = descriptor->bEndpointAddress;
  struct hw_endpoint* ep = hw_endpoint_get_by_addr(address);
  TU_VERIFY(ep->hw_data_buf != NULL && ep->buffer_control != NULL);
  const uint32_t mask = 1u << (2u * tu_edpt_number(address) +
                              (tu_edpt_dir(address) == TUSB_DIR_OUT ? 1u : 0u));
  const uint32_t interrupts = save_and_disable_interrupts();
  bool result = usb_cam_abort_iso(ep, mask);
  if (result) result = usb_cam_original_iso_activate(rhport, descriptor);
  else recovery_required = true;
  restore_interrupts(interrupts);
  return result;
}
