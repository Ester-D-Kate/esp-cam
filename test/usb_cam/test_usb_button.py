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
if __name__=='__main__':unittest.main()
