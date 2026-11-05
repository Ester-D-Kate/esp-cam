#!/bin/sh
set -eu
project_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mkdir -p "$project_dir/.pio/host-tests"
tinyusb_dir=${USB_CAM_TINYUSB_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-picosdk/lib/tinyusb/src}
if [ ! -f "$tinyusb_dir/tusb.h" ]; then
  echo "Install Pico dependencies first: pio pkg install -d pico -e zero_camera" >&2
  exit 1
fi
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$project_dir/pico/src" "$project_dir/test/usb_cam/test_dma_receive.c" \
  -o "$project_dir/.pio/host-tests/dma-receive-tests"
"$project_dir/.pio/host-tests/dma-receive-tests"
"${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$project_dir/pico/src" "$project_dir/test/usb_cam/test_iso_restart.c" \
  -o "$project_dir/.pio/host-tests/iso-restart-tests"
"$project_dir/.pio/host-tests/iso-restart-tests"
for name in protocol configuration runtime_protocol commands; do
  "${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I"$project_dir/shared" "$project_dir/test/usb_cam/test_$name.cpp" \
    -o "$project_dir/.pio/host-tests/$name-tests"
  "$project_dir/.pio/host-tests/$name-tests"
done
sensor_dir=${USB_CAM_SENSOR_DIR:-${PLATFORMIO_CORE_DIR:-$HOME/.platformio}/packages/framework-arduinoespressif32/tools/sdk/esp32/include/esp32-camera/driver/include}
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$project_dir/shared" -I"$project_dir/src/usb_cam" -I"$sensor_dir" \
  "$project_dir/test/usb_cam/test_sensor_settings.cpp" -o "$project_dir/.pio/host-tests/sensor-tests"
"$project_dir/.pio/host-tests/sensor-tests"
"${CXX:-clang++}" -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
  -I"$project_dir/test/usb_cam/stubs" -I"$project_dir/shared" -I"$project_dir/src/usb_cam" \
  -I"$sensor_dir" -I"$sensor_dir/../../conversions/include" \
  "$project_dir/src/usb_cam/camera_control.cpp" "$project_dir/test/usb_cam/test_camera_control.cpp" \
  -o "$project_dir/.pio/host-tests/camera-control-tests"
"$project_dir/.pio/host-tests/camera-control-tests"
for fast in 0 1; do
  for name in usb_descriptors video_negotiation; do
    extra_source=""
    if [ "$name" = video_negotiation ]; then extra_source="$project_dir/pico/src/video_driver.c"; fi
    "${CC:-clang}" -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
      -DUSB_CAM_ENABLE_60FPS="$fast" -DCFG_TUSB_MCU=OPT_MCU_NONE -DCFG_TUSB_OS=OPT_OS_NONE \
      -DTUP_DCD_ENDPOINT_MAX=16 \
      -I"$project_dir/test/usb_cam/stubs" -I"$project_dir/shared" \
      -I"$project_dir/pico/src" -I"$tinyusb_dir" \
      "$project_dir/pico/src/usb_descriptors.c" ${extra_source:+"$extra_source"} \
      "$project_dir/test/usb_cam/test_$name.c" -o "$project_dir/.pio/host-tests/$name-tests"
    "$project_dir/.pio/host-tests/$name-tests"
  done
done
