import importlib.util
from pathlib import Path
import struct
import unittest
path=Path(__file__).resolve().parents[2]/'tools'/'usb_button.py'
spec=importlib.util.spec_from_file_location('usb_button',path)
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

def report(revision,flags=2,sequence=0,session=7,time=100):
    return bytes([1])+struct.pack('<BBIIIH',1,flags,sequence,time,session,revision)

class ReaderTests(unittest.TestCase):
    def test_bad_reports(self):
        for raw in (b'',b'\x01',report(1)[:-1],bytes([2])+report(1)[1:],report(1,flags=128)):
            with self.assertRaises(ValueError):module.decode_report(raw)
    def test_snapshot_never_clicks_and_stale_packets_do_not_replay(self):
        reader=module.ButtonReader(module.decode_report(report(10,flags=7,sequence=2)))
        self.assertIsNone(reader.feed(module.decode_report(report(9,flags=3,sequence=1))))
        self.assertEqual(reader.feed(module.decode_report(report(11,flags=4)))['event'],'unavailable')
        self.assertEqual(reader.feed(module.decode_report(report(12,flags=7,session=8)))['event'],'sync')
        self.assertEqual(reader.feed(module.decode_report(report(13,flags=2,sequence=1,session=8)))['event'],'release')
        self.assertEqual(reader.feed(module.decode_report(report(14,flags=3,sequence=2,session=8)))['event'],'press')
        self.assertIsNone(reader.feed(module.decode_report(report(14,flags=3,sequence=2,session=8))))
    def test_revision_wrap_and_loss_cancel_gestures(self):
        reader=module.ButtonReader(module.decode_report(report(65535,flags=6)))
        self.assertEqual(reader.feed(module.decode_report(report(0,flags=6,session=8)))['event'],'sync')
        self.assertEqual(reader.feed(module.decode_report(report(1,flags=3,sequence=1,session=8)))['event'],'press')
        self.assertEqual(reader.feed(module.decode_report(report(2,flags=12,sequence=1,session=8)))['event'],'unavailable')
        self.assertIsNone(reader.feed(module.decode_report(report(3,flags=3,sequence=2,session=8))))
        self.assertEqual(reader.feed(module.decode_report(report(4,flags=14,session=9)))['event'],'sync')
    def test_missing_input_report_is_not_invented_as_a_click(self):
        reader=module.ButtonReader(module.decode_report(report(1,flags=6)))
        reader.feed(module.decode_report(report(2,flags=6,session=8)))
        result=reader.feed(module.decode_report(report(4,flags=2,sequence=2,session=8)))
        self.assertEqual(result['event'],'loss')
        self.assertIsNone(reader.feed(module.decode_report(report(5,flags=3,sequence=3,session=8))))
class SessionTests(unittest.TestCase):
    class Device:
        def __init__(self, snapshot, written=2):
            self.snapshot=snapshot;self.written=written;self.calls=[]
        def send_feature_report(self, data):
            self.calls.append(('set',bytes(data)));return self.written
        def get_input_report(self, report_id, length):
            self.calls.append(('get',report_id,length));return self.snapshot
    def test_resync_precedes_snapshot_and_fences_inflight_old_sync_and_press(self):
        device=self.Device(report(13,flags=4))
        reader=module.start_session(device)
        self.assertEqual(device.calls,[('set',bytes([2,1])),('get',1,17)])
        self.assertIsNone(reader.feed(module.decode_report(report(11,flags=6))))
        self.assertIsNone(reader.feed(module.decode_report(report(12,flags=3,sequence=1))))
        self.assertEqual(reader.feed(module.decode_report(report(14,flags=6,session=8)))['event'],'sync')
        self.assertEqual(reader.feed(module.decode_report(report(15,flags=3,sequence=1,session=8)))['event'],'press')
    def test_handshake_completed_before_snapshot_accepts_next_release_only(self):
        reader=module.start_session(self.Device(report(15,flags=7,sequence=2,session=8)))
        self.assertIsNone(reader.feed(module.decode_report(report(15,flags=7,sequence=2,session=8))))
        self.assertEqual(reader.feed(module.decode_report(report(16,flags=2,sequence=3,session=8)))['event'],'release')
    def test_short_feature_write_is_not_a_successful_resync(self):
        device=self.Device(report(13,flags=4),written=1)
        with self.assertRaises(OSError):module.start_session(device)
        self.assertEqual(len(device.calls),1)
    def test_automatic_selection_pins_first_serial_across_reconnects(self):
        class Hid:
            devices=[]
            def enumerate(self,*_):return self.devices
        def camera(serial):return dict(product_id=0x4064,usage_page=0xff00,usage=1,serial_number=serial)
        hid=Hid();hid.devices=[camera('first')]
        selection=module.CameraSelection()
        self.assertEqual(selection.find(hid)['serial_number'],'first')
        hid.devices=[camera('second')]
        with self.assertRaises(OSError):selection.find(hid)
        hid.devices=[camera('second'),camera('first')]
        self.assertEqual(selection.find(hid)['serial_number'],'first')
if __name__=='__main__':unittest.main()
