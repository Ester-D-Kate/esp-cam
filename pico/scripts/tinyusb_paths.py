"""Add SDK USB headers before PlatformIO constructs the application objects."""
from pathlib import Path

Import("env")

sdk = Path(env.PioPlatform().get_package_dir("framework-picosdk"))
env.Append(CPPPATH=[str(sdk / "lib" / "tinyusb" / "src")])
env.Append(CXXFLAGS=["-std=gnu++17"])
