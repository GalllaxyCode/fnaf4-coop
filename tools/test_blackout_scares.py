import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
import wave
import zlib

spec = importlib.util.spec_from_file_location('blackout_scares', Path(__file__).with_name('blackout-scares.py'))
blackout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(blackout)


class BlackoutTests(unittest.TestCase):
    def image(self, alpha=False):
        # A two-pixel, one-row 24-bit image with an optional 4-byte alpha row.
        raw = bytearray(32)
        struct.pack_into('<hh', raw, 12, 2, 1)
        raw[16] = 4
        raw[17] = 16 if alpha else 0
        raw[28:31] = b'\x01\x02\x03'
        return bytes(raw) + b'\x11\x22\x33\x44\x55\x66' + (b'\x03\x04\x00\x00' if alpha else b'')

    def test_blackens_color_and_keeps_structure(self):
        for alpha in (False, True):
            original = self.image(alpha)
            result = blackout.black_image(original)
            self.assertEqual(result[:28], original[:28])
            self.assertEqual(result[28:31], b'\xff' * 3)
            self.assertEqual(result[32:38], bytes(6))
            if alpha:
                self.assertEqual(result[38:42], b'\xff' * 4)

    def test_rejects_unsupported_and_truncated_images(self):
        image = bytearray(self.image())
        image[16] = 2
        with self.assertRaises(ValueError):
            blackout.black_image(image)
        with self.assertRaises(ValueError):
            blackout.black_image(self.image()[:-1])

    def test_silences_pcm_without_changing_wav_metadata(self):
        sink = io.BytesIO()
        with wave.open(sink, 'wb') as wav:
            wav.setparams((2, 2, 44100, 3, 'NONE', 'not compressed'))
            wav.writeframes(b'\x01\x02\x03\x04' * 3)
        name = 'scream2\0'.encode('utf-16le')
        original = name + sink.getvalue()
        silent = blackout.silence_sound(original, len(name) // 2)
        self.assertEqual(silent[:len(name)], name)
        with wave.open(io.BytesIO(silent[len(name):]), 'rb') as wav:
            self.assertEqual(wav.getparams()[:4], (2, 2, 44100, 3))
            self.assertEqual(wav.readframes(3), bytes(12))

    def test_rejects_invalid_sound(self):
        with self.assertRaises(ValueError):
            blackout.silence_sound(b'not a wave', 0)

    def test_replacement_preserves_record_length_and_neighbors(self):
        raw = b'hello' * 200
        block = zlib.compress(raw, 9) + bytes(30)
        stream = io.BytesIO(b'prefix' + block + b'suffix')
        stream.seek(6 + len(block))
        blackout.replace_block(stream, 6, len(block), bytes(len(raw)))
        result = stream.getvalue()
        self.assertEqual(len(result), 6 + len(block) + 6)
        self.assertEqual(result[:6], b'prefix')
        self.assertEqual(result[-6:], b'suffix')
        self.assertEqual(zlib.decompress(result[6:-6]), bytes(len(raw)))

    def test_too_large_replacement_does_not_write(self):
        stream = io.BytesIO(b'original bytes')
        stream.seek(5)
        with self.assertRaises(ValueError):
            blackout.replace_block(stream, 2, 1, bytes(range(100)))
        self.assertEqual(stream.getvalue(), b'original bytes')
        self.assertEqual(stream.tell(), 5)


if __name__ == '__main__':
    unittest.main()
