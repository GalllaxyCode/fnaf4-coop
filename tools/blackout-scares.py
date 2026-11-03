#!/usr/bin/env python3
"""Create a separate FNAF 4 executable with black, silent scare assets.

Only the two documented Steam build 288 executables are supported. Image and
sound records are replaced in place in a copy, keeping every pack offset and
all gameplay events unchanged. No game content is distributed by this tool.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import zlib

HASHES = {
    '082a7ecd7369dff1e4bf433b3e8e73ee10fa200afb6509fbe47f551dd3c9c314': 'normal',
    'cb31f6ce460beff1bb4d1a9ae7f16e346c58a743af9d01a41a10489e3df43354': 'halloween',
}
# Image IDs come from the supported builds' death objects, the minigame
# scare animations, and the Nightmare frame. Image 180 is shared with a
# normal carpet animation and already consists entirely of black pixels.
# The manifest also includes the corresponding Extras scare images.
SOUNDS = {'scream2', 'screamshort', 'scream3', 'distortion'}


def digest(path):
    sha = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            sha.update(block)
    return sha.hexdigest()


def read_exact(stream, size):
    data = stream.read(size)
    if len(data) != size:
        raise ValueError('truncated game data')
    return data


def black_image(data):
    raw = bytearray(data)
    if len(raw) < 32 or raw[16] != 4 or raw[17] & 15:
        raise ValueError('unsupported image format')
    width, height = struct.unpack_from('<hh', raw, 12)
    if not (0 < width <= 4096 and 0 < height <= 4096):
        raise ValueError('invalid image dimensions')
    colors = (width + (width * 3) % 2) * 3 * height
    alpha = (width + (4 - width % 4) % 4) * height if raw[17] & 16 else 0
    if len(raw) < 32 + colors + alpha:
        raise ValueError('truncated image pixels')
    # Black is visible even when the image uses a transparent colour key.
    raw[28:31] = b'\xff\xff\xff'
    raw[32:32 + colors] = bytes(colors)
    if alpha:
        raw[32 + colors:32 + colors + alpha] = b'\xff' * alpha
    return bytes(raw)


def silence_sound(data, name_length):
    raw = bytearray(data)
    start = name_length * 2
    if raw[start:start + 4] != b'RIFF' or raw[start + 8:start + 12] != b'WAVE':
        raise ValueError('sound is not a WAV file')
    pos = start + 12
    fmt = None
    pcm = None
    while pos + 8 <= len(raw):
        tag = raw[pos:pos + 4]
        length, = struct.unpack_from('<I', raw, pos + 4)
        body = pos + 8
        if body + length > len(raw):
            raise ValueError('truncated WAV chunk')
        if tag == b'fmt ':
            if length < 16:
                raise ValueError('invalid WAV format')
            codec, _, _, _, _, bits = struct.unpack_from('<HHIIHH', raw, body)
            fmt = (codec, bits)
        elif tag == b'data':
            pcm = (body, body + length)
        pos = body + length + (length & 1)
    if fmt != (1, 16) or pcm is None:
        raise ValueError('unsupported WAV encoding')
    start, end = pcm
    raw[start:end] = bytes(end - start)
    return bytes(raw)


def replace_block(stream, offset, packed_size, raw):
    encoded = zlib.compress(raw, 9)
    if len(encoded) > packed_size:
        raise ValueError('replacement does not fit the original record')
    encoded += bytes(packed_size - len(encoded))
    if zlib.decompress(encoded) != raw:
        raise ValueError('replacement validation failed')
    end = stream.tell()
    stream.seek(offset)
    stream.write(encoded)
    stream.seek(end)


def patch(path, edition, images):
    image_ids = set(images[edition])
    seen_images, seen_sounds = set(), set()
    with path.open('r+b') as stream:
        stream.seek(0x18ed25)
        if read_exact(stream, 4) != b'PAMU':
            raise ValueError('unexpected game header')
        stream.seek(12, 1)
        while True:
            chunk, flags, size = struct.unpack('<HHi', read_exact(stream, 8))
            if size < 0:
                raise ValueError('invalid chunk size')
            end = stream.tell() + size
            if chunk == 0x7f7f:
                break
            if chunk == 0x6666:
                if flags:
                    raise ValueError('unsupported image bank')
                count, = struct.unpack('<I', read_exact(stream, 4))
                for _ in range(count):
                    handle, raw_size, packed = struct.unpack('<III', read_exact(stream, 12))
                    offset = stream.tell()
                    if handle - 1 not in image_ids:
                        stream.seek(packed, 1)
                        continue
                    raw = zlib.decompress(read_exact(stream, packed))
                    if len(raw) != raw_size:
                        raise ValueError('invalid image record size')
                    replace_block(stream, offset, packed, black_image(raw))
                    seen_images.add(handle - 1)
            elif chunk == 0x6668:
                if flags:
                    raise ValueError('unsupported sound bank')
                count, = struct.unpack('<I', read_exact(stream, 4))
                for _ in range(count):
                    _, _, _, raw_size, sound_flags, _, name_len = struct.unpack('<7I', read_exact(stream, 28))
                    if sound_flags & 32:
                        raise ValueError('unsupported disk-streamed sound')
                    packed, = struct.unpack('<I', read_exact(stream, 4))
                    offset = stream.tell()
                    raw = zlib.decompress(read_exact(stream, packed))
                    if len(raw) != raw_size:
                        raise ValueError('invalid sound record size')
                    name = raw[:name_len * 2].decode('utf-16le').rstrip('\0')
                    if name in SOUNDS:
                        replace_block(stream, offset, packed, silence_sound(raw, name_len))
                        seen_sounds.add(name)
            else:
                stream.seek(size, 1)
            if stream.tell() != end:
                raise ValueError('unexpected bank length')
    if seen_images != image_ids or seen_sounds != SOUNDS:
        raise ValueError('not all expected scare assets were found')
    return len(seen_images), len(seen_sounds)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path, help='original supported game executable')
    parser.add_argument('output', type=Path, help='new executable to create; must not already exist')
    args = parser.parse_args()
    edition = HASHES.get(digest(args.source))
    if edition is None:
        raise ValueError('unsupported executable; use the unmodified supported Steam build')
    if args.output.exists():
        raise FileExistsError(f'output already exists: {args.output}')
    images = json.loads(Path(__file__).with_name('blackout-images.json').read_text(encoding='utf-8'))
    fd, temp = tempfile.mkstemp(prefix='.blackout-', dir=args.output.parent)
    os.close(fd)
    try:
        shutil.copyfile(args.source, temp)
        result = patch(Path(temp), edition, images)
        # Link only if the destination still does not exist, so a racing process
        # cannot cause an existing file to be overwritten.
        os.link(temp, args.output)
    finally:
        os.unlink(temp)
    print(f'Created {args.output}: blackened {result[0]} images and silenced {result[1]} sounds.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, UnicodeError, zlib.error, struct.error) as exc:
        print(f'Error: {exc}', file=sys.stderr)
        sys.exit(1)
