# /// script
# requires-python = ">=3.11"
# dependencies = ["hidapi==0.15.0"]
# ///
"""Read the camera's raw HID button without opening its video or serial ports."""
import argparse
import json
import struct
import sys
import time

PRESSED, AVAILABLE, SYNC, LOSS = 1, 2, 4, 8


def decode_report(raw):
    data = bytes(raw)
    if len(data) != 17 or data[0] != 1:
        raise ValueError("Expected report ID 1 and 16 payload bytes")
    version, flags, sequence, timestamp, session, revision = struct.unpack("<BBIIIH", data[1:])
    if version != 1 or flags & ~15 or sequence > 0x7fffffff:
        raise ValueError("Unsupported button report")
    if (flags & AVAILABLE and not session) or (flags & PRESSED and not flags & AVAILABLE):
        raise ValueError("Invalid button availability/state")
    return {"flags": flags, "sequence": sequence, "timestamp_ms": timestamp,
            "session_id": session, "revision": revision, "pressed": bool(flags & PRESSED)}


class ButtonReader:
    """Fence old input with GET_REPORT, then wait for the requested fresh snapshot."""
    def __init__(self, snapshot):
        self.revision = snapshot["revision"]
        self.sequence = snapshot["sequence"]
        self.session = snapshot["session_id"]
        self.ready = False

    def feed(self, report):
        delta = (report["revision"] - self.revision) & 0xffff
        if not delta or delta >= 0x8000:
            return None
        self.revision = report["revision"]
        event = {key: value for key, value in report.items() if key != "flags"}
        event["lost"] = bool(report["flags"] & LOSS)
        if not report["flags"] & AVAILABLE:
            self.ready = False
            event["event"] = "unavailable"
        elif report["flags"] & SYNC:
            self.ready = True
            self.session, self.sequence = report["session_id"], report["sequence"]
            event["event"] = "sync"
        elif not self.ready:
            return None
        elif (delta != 1 or report["session_id"] != self.session or
              (report["sequence"] - self.sequence) & 0x7fffffff != 1):
            self.ready = False
            event.update(event="loss", lost=True)
        else:
            self.sequence = report["sequence"]
            event["event"] = "press" if report["pressed"] else "release"
        return event


def on_button(event):
    """Customize this callback. Only press/release are actions; sync/loss reset gestures.

    Use timestamp_ms (ESP debounce time) for multi-click timing. Differences wrap
    modulo 2**32. A sync with pressed=True means 'already held', not a new press.
    This example prints events only; it never records, types keys or runs commands.
    """
    print(json.dumps(event), flush=True)


def camera_buttons(hid, serial=None):
    return [device for device in hid.enumerate(0xcafe, 0)
            if 0x4060 <= device["product_id"] <= 0x4065
            and device.get("usage_page") == 0xff00 and device.get("usage") == 1
            and (serial is None or device.get("serial_number") == serial)]


def main():
    import hid
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--list", action="store_true", help="List matching HID interfaces")
    parser.add_argument("--serial", help="Select one camera when several are connected")
    parser.add_argument("--duration", type=float, help="Stop after this many seconds (default: until Ctrl-C)")
    args = parser.parse_args()
    if args.duration is not None and args.duration <= 0:
        parser.error("--duration must be positive")
    if args.list:
        for device in camera_buttons(hid, args.serial):
            print(json.dumps({key: device.get(key) for key in
                              ("vendor_id", "product_id", "serial_number", "product_string", "usage_page", "usage")}))
        return
    deadline = time.monotonic() + args.duration if args.duration else float("inf")
    last_error = None
    while time.monotonic() < deadline:
        device = None
        try:
            matches = camera_buttons(hid, args.serial)
            if len(matches) > 1:
                raise SystemExit("Several camera buttons found; choose --serial from --list")
            if not matches:
                raise OSError("Waiting for the camera's HID button interface")
            device = hid.device()
            device.open_path(matches[0]["path"])
            reader = ButtonReader(decode_report(device.get_input_report(1, 17)))
            if device.send_feature_report(bytes([2, 1])) != 2:
                raise OSError("Could not request a fresh button snapshot")
            last_error = None
            while time.monotonic() < deadline:
                raw = device.read(17, timeout_ms=250)
                if not raw:
                    continue
                try:
                    event = reader.feed(decode_report(raw))
                except ValueError:
                    # Invalid input cannot become an action; fence and resync.
                    reader = ButtonReader(decode_report(device.get_input_report(1, 17)))
                    device.send_feature_report(bytes([2, 1]))
                    on_button({"event": "loss", "lost": True})
                    continue
                if event is not None:
                    on_button(event)
                    if event["event"] == "loss":
                        device.send_feature_report(bytes([2, 1]))
        except OSError as error:
            if str(error) != last_error:
                print(str(error), file=sys.stderr, flush=True)
                last_error = str(error)
            on_button({"event": "disconnected"})
            time.sleep(min(1, max(0, deadline - time.monotonic())))
        finally:
            if device is not None:
                device.close()


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
