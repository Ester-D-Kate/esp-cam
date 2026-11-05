"""Compile the SDK's pinned TinyUSB with the application's UVC configuration."""
from pathlib import Path

Import("env")

sdk = Path(env.PioPlatform().get_package_dir("framework-picosdk"))
tinyusb = sdk / "lib" / "tinyusb" / "src"
env.Append(CPPPATH=[str(tinyusb)])
env.Append(LIBS=[env.BuildLibrary(
    "$BUILD_DIR/TinyUSB",
    str(tinyusb),
    src_filter="-<*> +<tusb.c> +<common/tusb_fifo.c> +<device/*.c> "
    "+<class/cdc/cdc_device.c> "
    "+<portable/raspberrypi/rp2040/*.c> -<portable/raspberrypi/rp2040/dcd_rp2040.c>",
)])
env.Append(LIBS=[env.BuildLibrary("$BUILD_DIR/PicoFix", str(sdk / "src" / "rp2_common" / "pico_fix"))])
