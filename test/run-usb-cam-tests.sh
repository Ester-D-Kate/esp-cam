#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$project_dir/.pio/host-tests"
tinyusb_dir=${USB_CAM_TINYUSB_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-picosdk/lib/tinyusb/src}
if [ ! -f "$tinyusb_dir/tusb.h" ]; then
  echo "Install Pico dependencies first: pio pkg install -d pico -e zero_vga30" >&2
  exit 1
fi
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$project_dir/pico/src" "$project_dir/test/usb_cam/test_iso_restart.c" \
  -o "$project_dir/.pio/host-tests/iso-restart-tests"
"$project_dir/.pio/host-tests/iso-restart-tests"
for profile in 1 2 3 4; do
  "${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DUSB_CAM_PROFILE="$profile" -I"$project_dir/shared" \
    "$project_dir/test/usb_cam/test_protocol.cpp" -o "$project_dir/.pio/host-tests/usb-cam-tests"
  "$project_dir/.pio/host-tests/usb-cam-tests"
  "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -DUSB_CAM_PROFILE="$profile" -DCFG_TUSB_MCU=OPT_MCU_NONE -DCFG_TUSB_OS=OPT_OS_NONE \
    -DTUP_DCD_ENDPOINT_MAX=16 \
    -I"$project_dir/test/usb_cam/stubs" -I"$project_dir/shared" \
    -I"$project_dir/pico/src" -I"$tinyusb_dir" \
    "$project_dir/pico/src/usb_descriptors.c" "$project_dir/test/usb_cam/test_usb_descriptors.c" \
    -o "$project_dir/.pio/host-tests/usb-descriptor-tests"
  "$project_dir/.pio/host-tests/usb-descriptor-tests"
done
