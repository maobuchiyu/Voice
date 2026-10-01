"""无需串口和第三方包即可运行的协议离线检查。"""

import struct
import unittest

from protocol import (COMMAND, FrameParser, STATUS, WAVEFORM, crc16, decode,
                      encode)


class ProtocolTests(unittest.TestCase):
    def test_v01_extended_status(self):
        payload = struct.pack("<BBHIHHIIIII", 1, 1, 0x300, 48000, 2048, 0, 1, 0, 0, 0, 10)
        payload += struct.pack("<IhH", 1000, -1200, 0)
        fields = decode(FrameParser().feed(encode(STATUS, 1, payload))[0])
        self.assertTrue(fields["tone_enabled"])
        self.assertEqual(fields["tone_peak_dbfs"], -12)
        self.assertEqual(fields["fft_length"], 2048)

    def test_user_status_frame(self):
        raw = bytes.fromhex(
            "A5 5A 01 01 9E 01 20 00 00 F4 2F 01 00 01 00 00 "
            "80 BB 00 00 00 00 00 00 FA 97 00 00 00 00 00 00 "
            "00 00 00 00 19 00 00 00 18 55 06 00 5C CC")
        parser = FrameParser()
        frames = parser.feed(raw[:9]) + parser.feed(raw[9:])
        self.assertEqual(len(frames), 1)
        self.assertEqual(parser.bad_frames, 0)
        frame = frames[0]
        self.assertEqual((frame.type, frame.seq, frame.sample_index),
                         (STATUS, 414, 19919872))
        fields = decode(frame)
        self.assertEqual(fields["sample_rate"], 48000)
        self.assertEqual(fields["completed_blocks"], 38906)
        self.assertEqual(fields["bad_frames"], 25)
        self.assertEqual(fields["uptime_ms"], 415000)

    def test_command_and_recovery_after_bad_crc(self):
        good = encode(COMMAND, 0x1234, b"\x02\x01")
        bad = bytearray(good)
        bad[-1] ^= 0x01
        parser = FrameParser()
        frames = parser.feed(b"\x00\xA5" + bad + good[:4])
        frames += parser.feed(good[4:])
        self.assertEqual(frames[0].seq, 0x1234)
        self.assertEqual(frames[0].payload, b"\x02\x01")
        self.assertEqual(parser.bad_frames, 1)
        self.assertEqual(int.from_bytes(good[-2:], "little"), crc16(good[2:-2]))

    def test_waveform_envelope(self):
        pairs = [(-1000 + i, 1000 + i) for i in range(64)]
        payload = struct.pack("<HH", 512, 64) + b"".join(
            struct.pack("<hh", low, high) for low, high in pairs)
        frame = FrameParser().feed(encode(WAVEFORM, 5, payload, 512))[0]
        self.assertEqual(decode(frame)["min_max"], pairs)
        self.assertEqual(frame.sample_index, 512)


if __name__ == "__main__":
    unittest.main()
