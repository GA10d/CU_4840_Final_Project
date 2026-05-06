#!/usr/bin/env python3
"""Convert PPM assets to little-endian RGB565 files.

Each output file is written next to its source with the same stem and a
.rgb565 suffix.

File format:
  offset  size  value
  0       4     ASCII magic "R565"
  4       2     uint16_le header size, currently 16
  6       2     uint16_le width
  8       2     uint16_le height
  10      2     uint16_le pixel format, currently 1 for RGB565 little-endian
  12      4     uint32_le pixel data size in bytes
  16      ...   width * height uint16_le RGB565 pixels, row-major
"""

from __future__ import annotations

import argparse
from pathlib import Path


RGB565_MAGIC = b"R565"
RGB565_HEADER_SIZE = 16
RGB565_PIXEL_FORMAT_LE = 1


def _read_token(data: bytes, pos: int) -> tuple[bytes, int]:
    n = len(data)

    while pos < n:
        byte = data[pos]
        if byte in b" \t\r\n":
            pos += 1
            continue
        if byte == ord("#"):
            while pos < n and data[pos] not in b"\r\n":
                pos += 1
            continue
        break

    if pos >= n:
        raise ValueError("unexpected end of file while reading PPM header")

    start = pos
    while pos < n and data[pos] not in b" \t\r\n#":
        pos += 1

    return data[start:pos], pos


def _skip_single_whitespace(data: bytes, pos: int) -> int:
    if pos < len(data) and data[pos] in b" \t\r\n":
        return pos + 1
    return pos


def _scale_to_255(value: int, max_value: int) -> int:
    if max_value == 255:
        return value
    return (value * 255 + max_value // 2) // max_value


def _pack_rgb565(r: int, g: int, b: int) -> int:
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def convert_ppm_file(path: Path, overwrite: bool = True) -> Path:
    data = path.read_bytes()
    pos = 0

    magic, pos = _read_token(data, pos)
    width_text, pos = _read_token(data, pos)
    height_text, pos = _read_token(data, pos)
    max_text, pos = _read_token(data, pos)

    if magic not in {b"P6", b"P3"}:
        raise ValueError(f"unsupported PPM magic {magic!r}")

    width = int(width_text)
    height = int(height_text)
    max_value = int(max_text)
    if width <= 0 or height <= 0:
        raise ValueError(f"invalid image size {width}x{height}")
    if max_value <= 0 or max_value > 255:
        raise ValueError(f"unsupported max value {max_value}")

    pixel_count = width * height
    pixel_data_size = pixel_count * 2
    out = bytearray(RGB565_HEADER_SIZE + pixel_data_size)
    out[0:4] = RGB565_MAGIC
    out[4:6] = RGB565_HEADER_SIZE.to_bytes(2, "little")
    out[6:8] = width.to_bytes(2, "little")
    out[8:10] = height.to_bytes(2, "little")
    out[10:12] = RGB565_PIXEL_FORMAT_LE.to_bytes(2, "little")
    out[12:16] = pixel_data_size.to_bytes(4, "little")

    if magic == b"P6":
        pos = _skip_single_whitespace(data, pos)
        expected = pixel_count * 3
        pixels = data[pos : pos + expected]
        if len(pixels) != expected:
            raise ValueError(
                f"truncated P6 data: expected {expected} bytes, got {len(pixels)}"
            )

        for i in range(pixel_count):
            src = i * 3
            r = _scale_to_255(pixels[src], max_value)
            g = _scale_to_255(pixels[src + 1], max_value)
            b = _scale_to_255(pixels[src + 2], max_value)
            packed = _pack_rgb565(r, g, b)
            dst = RGB565_HEADER_SIZE + i * 2
            out[dst] = packed & 0xFF
            out[dst + 1] = (packed >> 8) & 0xFF
    else:
        for i in range(pixel_count):
            r_text, pos = _read_token(data, pos)
            g_text, pos = _read_token(data, pos)
            b_text, pos = _read_token(data, pos)
            r = _scale_to_255(int(r_text), max_value)
            g = _scale_to_255(int(g_text), max_value)
            b = _scale_to_255(int(b_text), max_value)
            packed = _pack_rgb565(r, g, b)
            dst = RGB565_HEADER_SIZE + i * 2
            out[dst] = packed & 0xFF
            out[dst + 1] = (packed >> 8) & 0xFF

    output_path = path.with_suffix(".rgb565")
    if output_path.exists() and not overwrite:
        raise FileExistsError(f"{output_path} already exists")
    output_path.write_bytes(out)
    return output_path


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Convert all .ppm files under an asset directory to .rgb565."
    )
    parser.add_argument(
        "asset_root",
        nargs="?",
        default="game_assets",
        type=Path,
        help="Directory to scan recursively. Defaults to game_assets.",
    )
    parser.add_argument(
        "--no-overwrite",
        action="store_true",
        help="Fail instead of replacing existing .rgb565 files.",
    )
    args = parser.parse_args()

    root = args.asset_root
    if not root.exists() or not root.is_dir():
        raise SystemExit(f"asset root does not exist or is not a directory: {root}")

    ppm_files = sorted(root.rglob("*.ppm"))
    converted = 0
    for ppm_path in ppm_files:
        output_path = convert_ppm_file(ppm_path, overwrite=not args.no_overwrite)
        converted += 1
        print(f"{ppm_path} -> {output_path}")

    print(f"converted {converted} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
