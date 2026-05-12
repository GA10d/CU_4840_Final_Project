#!/usr/bin/env python3
"""Convert PPM assets to little-endian RGB565 files.

Each output file is written next to its source with the same stem and a
.rgb565 suffix.

File format:
  offset  size  value
  0       4     ASCII file signature "R565"
  4       2     uint16_le header size, currently 16
  6       2     uint16_le width
  8       2     uint16_le height
  10      2     uint16_le pixel format, currently 1 for RGB565 little-endian
  12      4     uint32_le pixel data size in bytes
  16      ...   width * height uint16_le RGB565 pixels, row-major
"""

from pathlib import Path


ASSET_ROOT = Path("game_assets")
RGB565_FILE_SIGNATURE = b"R565"
RGB565_HEADER_SIZE = 16
RGB565_PIXEL_FORMAT_LE = 1


def _read_ppm_value(data: bytes, pos: int) -> tuple[bytes, int]:
    """Read one meaningful value from a PPM byte stream.

    English:
      What it does: Skips whitespace and PPM comments, then returns the next
      value plus the new read position.
      Why use it: PPM files can contain comments and flexible spacing, so this
      keeps the parser simple and consistent for header fields.

    中文:
      功能: 跳过空格、换行和 PPM 注释，然后读取下一个有效值，并返回新的读取位置。
      用途: PPM 文件允许注释和不同格式的空白字符，用这个函数可以统一处理
      PPM 头部字段。
    """
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
    """Skip the one separator byte before raw P6 pixel data.

    English:
      What it does: Advances the read position by one byte if that byte is
      whitespace.
      Why use it: In P6 PPM files, one whitespace character separates the text
      header from the binary RGB bytes. Skipping only one byte avoids eating
      valid pixel data that might look like whitespace.

    中文:
      功能: 如果当前位置是空白字符，就只向后移动一个字节。
      用途: P6 PPM 的文本头部和二进制像素数据之间只有一个空白分隔符。
      这里只跳过一个字节，避免误把像素数据中看起来像空白的字节删掉。
    """
    if pos < len(data) and data[pos] in b" \t\r\n":
        return pos + 1
    return pos


def _scale_to_255(value: int, max_value: int) -> int:
    """Scale a PPM color channel to the standard 0-255 range.

    English:
      What it does: Converts a channel value from the PPM file's declared
      maximum value into an 8-bit channel value.
      Why use it: RGB565 packing expects normal 8-bit RGB input, while PPM
      files may use a max value lower than 255.

    中文:
      功能: 根据 PPM 文件声明的最大颜色值，把颜色通道转换到 0-255 范围。
      用途: RGB565 打包逻辑需要标准 8-bit RGB 输入，但 PPM 文件的最大值
      不一定是 255。
    """
    if max_value == 255:
        return value

    scaled_value = value * 255
    rounding_offset = max_value // 2
    rounded_scaled_value = scaled_value + rounding_offset
    normalized_value = rounded_scaled_value // max_value

    return normalized_value


def _pack_rgb565(r: int, g: int, b: int) -> int:
    """Pack 8-bit red, green, and blue channels into one RGB565 pixel.

    English:
      What it does: Keeps 5 bits of red, 6 bits of green, and 5 bits of blue,
      then combines them into one 16-bit integer.
      Why use it: The VGA/game renderer uses RGB565 pixels because they are
      compact and match the 16-bit pixel format expected by the project.

    中文:
      功能: 保留红色 5 位、绿色 6 位、蓝色 5 位，并合成为一个 16-bit 像素值。
      用途: VGA/游戏渲染器使用 RGB565，因为它更省空间，并且符合本项目
      需要的 16-bit 像素格式。
    """
    red_5_bits = r >> 3
    green_6_bits = g >> 2
    blue_5_bits = b >> 3

    red_positioned = red_5_bits << 11
    green_positioned = green_6_bits << 5
    blue_positioned = blue_5_bits

    rgb565_pixel = red_positioned | green_positioned | blue_positioned

    return rgb565_pixel


def convert_ppm_file(path: Path) -> Path:
    """Convert one PPM image file into this project's .rgb565 asset format.

    English:
      What it does: Reads a P6 PPM file, validates its header, converts every
      RGB pixel into little-endian RGB565, and writes a .rgb565 file next to
      the source image.
      Why use it: It turns editable/exportable PPM image assets into the binary
      asset format that the game software and VGA renderer can load efficiently.

    中文:
      功能: 读取 P6 PPM 文件，检查头部信息，把每个 RGB 像素转换为
      little-endian RGB565，并在原图旁边生成 .rgb565 文件。
      用途: 把方便编辑或导出的 PPM 图片资源转换成本项目软件和 VGA 渲染器
      可以高效读取的二进制资源格式。
    """
    data = path.read_bytes()
    pos = 0

    ppm_format, pos = _read_ppm_value(data, pos)
    width_text, pos = _read_ppm_value(data, pos)
    height_text, pos = _read_ppm_value(data, pos)
    max_text, pos = _read_ppm_value(data, pos)

    if ppm_format != b"P6":
        raise ValueError(f"unsupported PPM format {ppm_format!r}")

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
    out[0:4] = RGB565_FILE_SIGNATURE
    out[4:6] = RGB565_HEADER_SIZE.to_bytes(2, "little")
    out[6:8] = width.to_bytes(2, "little")
    out[8:10] = height.to_bytes(2, "little")
    out[10:12] = RGB565_PIXEL_FORMAT_LE.to_bytes(2, "little")
    out[12:16] = pixel_data_size.to_bytes(4, "little")

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

    output_path = path.with_suffix(".rgb565")
    output_path.write_bytes(out)
    return output_path


def main() -> int:
    """Batch-convert all PPM assets under the default game_assets folder.

    English:
      What it does: Finds every .ppm file below game_assets, converts each one,
      and prints a short conversion log.
      Why use it: It lets the project regenerate all RGB565 image assets with
      one command instead of converting files manually one by one.

    中文:
      功能: 递归查找 game_assets 目录下所有 .ppm 文件，逐个转换，
      并打印转换记录。
      用途: 让项目可以用一个命令重新生成所有 RGB565 图片资源，不需要手动
      一个一个转换。
    """
    root = ASSET_ROOT
    if not root.exists() or not root.is_dir():
        raise SystemExit(f"asset root does not exist or is not a directory: {root}")

    ppm_files = sorted(root.rglob("*.ppm"))
    converted = 0
    for ppm_path in ppm_files:
        output_path = convert_ppm_file(ppm_path)
        converted += 1
        print(f"{ppm_path} -> {output_path}")

    print(f"converted {converted} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
