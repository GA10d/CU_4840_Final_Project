#!/usr/bin/env python3
"""Convert .vgm/.vgz files to .wav.

The builtin backend is intentionally dependency-free and supports the
SN76489/SN76496 PSG command set used by many 8-bit VGM files. For more complex
VGMs, you can optionally use either ``vgmplay`` or a local ``py-vgmplayer``
build when those backends are available on the host machine.
"""

from __future__ import annotations

import argparse
import gzip
import importlib.util
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import wave
from dataclasses import dataclass
from pathlib import Path


VGM_MAGIC = b"Vgm "
VGM_STREAM_RATE = 44100
DEFAULT_CHUNK_SAMPLES = 4096


class ConversionError(RuntimeError):
    """Raised when conversion cannot proceed."""


def read_u8(data: bytes, offset: int) -> int:
    return data[offset] if offset < len(data) else 0


def read_u16(data: bytes, offset: int) -> int:
    if offset + 2 > len(data):
        return 0
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    if offset + 4 > len(data):
        return 0
    return struct.unpack_from("<I", data, offset)[0]


def parity(value: int) -> int:
    return bin(value).count("1") & 1


def clamp_i16(value: float) -> int:
    if value > 32767:
        return 32767
    if value < -32768:
        return -32768
    return int(round(value))


def load_vgm_bytes(path: Path) -> bytes:
    raw = path.read_bytes()
    if raw[:2] == b"\x1f\x8b" or path.suffix.lower() == ".vgz":
        raw = gzip.decompress(raw)
    if raw[:4] != VGM_MAGIC:
        raise ConversionError(f"{path} is not a valid VGM/VGZ file.")
    return raw


@dataclass
class VgmHeader:
    version: int
    data_offset: int
    loop_offset: int
    total_samples: int
    loop_samples: int
    sn_clock: int
    sn_feedback: int
    sn_shift_width: int
    sn_flags: int


def parse_header(data: bytes) -> VgmHeader:
    version = read_u32(data, 0x08)
    data_relative = read_u32(data, 0x34) if version >= 0x00000150 else 0
    data_offset = 0x34 + data_relative if data_relative else 0x40
    loop_relative = read_u32(data, 0x1C)
    loop_offset = 0x1C + loop_relative if loop_relative else 0

    sn_clock = read_u32(data, 0x0C) & 0x3FFFFFFF
    sn_feedback = read_u16(data, 0x28)
    sn_shift_width = read_u8(data, 0x2A)
    sn_flags = read_u8(data, 0x2B) if version >= 0x00000151 else 0

    if version <= 0x00000101:
        sn_feedback = sn_feedback or 0x0009
        sn_shift_width = sn_shift_width or 16
    else:
        sn_feedback = sn_feedback or 0x0009
        sn_shift_width = sn_shift_width or 16

    if data_offset >= len(data):
        raise ConversionError("VGM data offset points past the end of the file.")

    return VgmHeader(
        version=version,
        data_offset=data_offset,
        loop_offset=loop_offset,
        total_samples=read_u32(data, 0x18),
        loop_samples=read_u32(data, 0x20),
        sn_clock=sn_clock,
        sn_feedback=sn_feedback,
        sn_shift_width=sn_shift_width,
        sn_flags=sn_flags,
    )


class WaitResampler:
    """Converts VGM wait units (44.1 kHz samples) to output samples."""

    def __init__(self, output_rate: int) -> None:
        self.output_rate = output_rate
        self.remainder = 0

    def convert(self, vgm_samples: int) -> int:
        total = self.remainder + vgm_samples * self.output_rate
        emitted = total // VGM_STREAM_RATE
        self.remainder = total % VGM_STREAM_RATE
        return emitted


class Sn76489Synth:
    """Very small SN76489 renderer suitable for UI sounds and simple music."""

    def __init__(
        self,
        clock_hz: int,
        feedback: int,
        shift_width: int,
        flags: int,
        sample_rate: int,
        gain: float,
    ) -> None:
        if clock_hz <= 0:
            raise ConversionError("Builtin backend requires a non-zero SN76489 clock.")
        self.clock_hz = float(clock_hz)
        self.feedback = feedback
        self.shift_width = shift_width
        self.flags = flags
        self.sample_rate = float(sample_rate)
        self.master_scale = 32767.0 * (gain / 4.0)

        self.tone_regs = [0, 0, 0]
        self.volume_regs = [15, 15, 15, 15]
        self.tone_phases = [0.0, 0.0, 0.0]
        self.noise_phase = 0.0
        self.noise_reg = 0
        self.latched_channel = 0
        self.latched_is_volume = False
        self.stereo_left = [True, True, True, True]
        self.stereo_right = [True, True, True, True]
        self.output_negate = -1.0 if (flags & 0x02) else 1.0

        self.volume_table = [
            1.0,
            0.794328,
            0.630957,
            0.501187,
            0.398107,
            0.316228,
            0.251189,
            0.199526,
            0.158489,
            0.125893,
            0.1,
            0.079433,
            0.063096,
            0.050119,
            0.039811,
            0.0,
        ]

        self.reset_lfsr()

    def reset_lfsr(self) -> None:
        self.lfsr = 1 << (self.shift_width - 1)

    def set_stereo(self, mask: int) -> None:
        for channel in range(4):
            self.stereo_right[channel] = bool(mask & (1 << channel))
            self.stereo_left[channel] = bool(mask & (1 << (channel + 4)))

    def write(self, value: int) -> None:
        if value & 0x80:
            self.latched_channel = (value >> 5) & 0x03
            self.latched_is_volume = bool(value & 0x10)
            payload = value & 0x0F

            if self.latched_is_volume:
                self.volume_regs[self.latched_channel] = payload
                return

            if self.latched_channel == 3:
                self.noise_reg = payload & 0x07
                self.reset_lfsr()
                self.noise_phase = 0.0
                return

            self.tone_regs[self.latched_channel] &= 0x3F0
            self.tone_regs[self.latched_channel] |= payload
            return

        payload = value & 0x3F
        if self.latched_is_volume:
            self.volume_regs[self.latched_channel] = payload & 0x0F
            return

        if self.latched_channel == 3:
            return

        self.tone_regs[self.latched_channel] &= 0x00F
        self.tone_regs[self.latched_channel] |= (payload & 0x3F) << 4

    def _effective_tone_period(self, period: int) -> int:
        if period == 0 and (self.flags & 0x01):
            return 0x400
        return period

    def _tone_sample(self, channel: int) -> float:
        period = self._effective_tone_period(self.tone_regs[channel])

        if period <= 1:
            return 1.0

        frequency = self.clock_hz / (32.0 * period)
        value = 1.0 if self.tone_phases[channel] < 0.5 else -1.0
        self.tone_phases[channel] += frequency / self.sample_rate
        if self.tone_phases[channel] >= 1.0:
            self.tone_phases[channel] -= int(self.tone_phases[channel])
        return value

    def _noise_shift_rate(self) -> float:
        rate_select = self.noise_reg & 0x03
        if rate_select == 0:
            return self.clock_hz / 512.0
        if rate_select == 1:
            return self.clock_hz / 1024.0
        if rate_select == 2:
            return self.clock_hz / 2048.0

        tone2_period = self._effective_tone_period(self.tone_regs[2])
        if tone2_period <= 1:
            return 0.0
        return self.clock_hz / (32.0 * tone2_period)

    def _shift_noise(self) -> None:
        if self.noise_reg & 0x04:
            incoming = parity(self.lfsr & self.feedback)
        else:
            incoming = self.lfsr & 0x01

        self.lfsr = (self.lfsr >> 1) | (incoming << (self.shift_width - 1))
        self.lfsr &= (1 << self.shift_width) - 1
        if self.lfsr == 0:
            self.reset_lfsr()

    def _noise_sample(self) -> float:
        shift_rate = self._noise_shift_rate()
        if shift_rate > 0.0:
            self.noise_phase += shift_rate / self.sample_rate
            while self.noise_phase >= 1.0:
                self.noise_phase -= 1.0
                self._shift_noise()
        return 1.0 if (self.lfsr & 0x01) else -1.0

    def next_sample(self) -> tuple[int, int]:
        left = 0.0
        right = 0.0

        for channel in range(3):
            sample = self._tone_sample(channel)
            amplitude = self.volume_table[self.volume_regs[channel]]
            if self.stereo_left[channel]:
                left += sample * amplitude
            if self.stereo_right[channel]:
                right += sample * amplitude

        noise = self._noise_sample()
        noise_amp = self.volume_table[self.volume_regs[3]]
        if self.stereo_left[3]:
            left += noise * noise_amp
        if self.stereo_right[3]:
            right += noise * noise_amp

        left *= self.output_negate * self.master_scale
        right *= self.output_negate * self.master_scale
        return clamp_i16(left), clamp_i16(right)


def render_samples(
    wav_file: wave.Wave_write,
    synth: Sn76489Synth,
    samples: int,
    chunk_size: int = DEFAULT_CHUNK_SAMPLES,
) -> None:
    remaining = samples
    while remaining > 0:
        chunk = min(remaining, chunk_size)
        buffer = bytearray(chunk * 4)
        offset = 0
        for _ in range(chunk):
            left, right = synth.next_sample()
            struct.pack_into("<hh", buffer, offset, left, right)
            offset += 4
        wav_file.writeframesraw(buffer)
        remaining -= chunk


def render_builtin(
    input_path: Path,
    output_path: Path,
    sample_rate: int,
    loops: int,
    gain: float,
) -> None:
    data = load_vgm_bytes(input_path)
    header = parse_header(data)
    synth = Sn76489Synth(
        clock_hz=header.sn_clock,
        feedback=header.sn_feedback,
        shift_width=header.sn_shift_width,
        flags=header.sn_flags,
        sample_rate=sample_rate,
        gain=gain,
    )
    resampler = WaitResampler(sample_rate)

    pc = header.data_offset
    loop_target = header.loop_offset if header.loop_offset >= header.data_offset else 0
    loops_left = loops

    with wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)

        while pc < len(data):
            command_offset = pc
            command = data[pc]
            pc += 1

            if command == 0x4F:
                if pc >= len(data):
                    raise ConversionError("Unexpected EOF after Game Gear stereo command.")
                synth.set_stereo(data[pc])
                pc += 1
                continue

            if command == 0x50:
                if pc >= len(data):
                    raise ConversionError("Unexpected EOF after PSG write command.")
                synth.write(data[pc])
                pc += 1
                continue

            if command == 0x61:
                wait = read_u16(data, pc)
                pc += 2
                render_samples(wav_file, synth, resampler.convert(wait))
                continue

            if command == 0x62:
                render_samples(wav_file, synth, resampler.convert(735))
                continue

            if command == 0x63:
                render_samples(wav_file, synth, resampler.convert(882))
                continue

            if 0x70 <= command <= 0x7F:
                render_samples(wav_file, synth, resampler.convert((command & 0x0F) + 1))
                continue

            if command == 0x66:
                if loop_target and loops_left > 0:
                    pc = loop_target
                    loops_left -= 1
                    continue
                break

            if command == 0x67:
                if pc + 6 > len(data) or data[pc] != 0x66:
                    raise ConversionError("Malformed VGM data block.")
                block_length = read_u32(data, pc + 2)
                pc += 6 + block_length
                continue

            raise ConversionError(
                "Builtin backend only supports SN76489 PSG commands, "
                f"but encountered 0x{command:02X} at 0x{command_offset:08X}. "
                "Use --backend vgmplay for more complex VGM chips."
            )


def render_with_vgmplay(input_path: Path, output_path: Path) -> None:
    vgmplay_path = shutil.which("vgmplay")
    if not vgmplay_path:
        raise ConversionError("The vgmplay executable was not found in PATH.")

    with tempfile.TemporaryDirectory(prefix="vgmplay_") as temp_dir:
        temp_root = Path(temp_dir)
        staged_input = temp_root / input_path.name
        shutil.copy2(input_path, staged_input)

        try:
            subprocess.run(
                [vgmplay_path, "-w", staged_input.name],
                cwd=temp_root,
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
            )
        except subprocess.CalledProcessError as exc:
            stderr = exc.stderr.strip() if exc.stderr else "vgmplay failed"
            raise ConversionError(stderr) from exc

        wav_candidates = sorted(temp_root.glob("*.wav"))
        if not wav_candidates:
            raise ConversionError("vgmplay finished but no WAV output was produced.")

        shutil.move(str(wav_candidates[0]), output_path)


def has_pyvgmplayer() -> bool:
    return importlib.util.find_spec("vgmplayer") is not None


def write_big_endian_pcm_as_wav(
    pcm_path: Path,
    output_path: Path,
    sample_rate: int,
) -> None:
    with pcm_path.open("rb") as pcm_file, wave.open(str(output_path), "wb") as wav_file:
        wav_file.setnchannels(2)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)

        while True:
            chunk = pcm_file.read(DEFAULT_CHUNK_SAMPLES * 4)
            if not chunk:
                break
            if len(chunk) % 2:
                raise ConversionError("py-vgmplayer produced an odd number of PCM bytes.")

            swapped = bytearray(len(chunk))
            swapped[0::2] = chunk[1::2]
            swapped[1::2] = chunk[0::2]
            wav_file.writeframesraw(swapped)


def render_with_pyvgmplayer(
    input_path: Path,
    output_path: Path,
    sample_rate: int,
    loops: int,
) -> None:
    if not has_pyvgmplayer():
        raise ConversionError(
            "The py-vgmplayer backend is not available. "
            "Add its package directory to PYTHONPATH first."
        )

    import vgmplayer

    with tempfile.NamedTemporaryFile(prefix="pyvgmplayer_", suffix=".pcm", delete=False) as raw_file:
        raw_path = Path(raw_file.name)

    try:
        raw_fd = os.open(raw_path, os.O_WRONLY | os.O_TRUNC)
        effective_loops = loops or 1
        try:
            result = vgmplayer.play(str(input_path), raw_fd, sample_rate, effective_loops)
        finally:
            try:
                os.close(raw_fd)
            except OSError:
                pass
        if result != 0:
            raise ConversionError(f"py-vgmplayer failed with exit code {result}.")
        write_big_endian_pcm_as_wav(raw_path, output_path, sample_rate)
    finally:
        raw_path.unlink(missing_ok=True)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert .vgm/.vgz files into .wav audio.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("input", type=Path, help="Input .vgm or .vgz file")
    parser.add_argument("output", type=Path, nargs="?", help="Output .wav file")
    parser.add_argument(
        "--backend",
        choices=["builtin", "auto", "vgmplay", "pyvgmplayer"],
        default="builtin",
        help="Conversion backend to use",
    )
    parser.add_argument(
        "--sample-rate",
        type=int,
        default=44100,
        help="Output WAV sample rate for the builtin and pyvgmplayer backends",
    )
    parser.add_argument(
        "--loops",
        type=int,
        default=0,
        help="How many extra times to follow the VGM loop point in builtin mode",
    )
    parser.add_argument(
        "--gain",
        type=float,
        default=0.8,
        help="Master output gain in builtin mode",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    input_path = args.input
    if not input_path.exists():
        parser.error(f"input file does not exist: {input_path}")

    output_path = args.output or input_path.with_suffix(".wav")
    if output_path.suffix.lower() != ".wav":
        output_path = output_path.with_suffix(".wav")

    try:
        if args.backend == "vgmplay":
            render_with_vgmplay(input_path, output_path)
        elif args.backend == "pyvgmplayer":
            render_with_pyvgmplayer(
                input_path=input_path,
                output_path=output_path,
                sample_rate=args.sample_rate,
                loops=args.loops,
            )
        elif args.backend == "auto" and shutil.which("vgmplay"):
            render_with_vgmplay(input_path, output_path)
        elif args.backend == "auto" and has_pyvgmplayer():
            render_with_pyvgmplayer(
                input_path=input_path,
                output_path=output_path,
                sample_rate=args.sample_rate,
                loops=args.loops,
            )
        else:
            render_builtin(
                input_path=input_path,
                output_path=output_path,
                sample_rate=args.sample_rate,
                loops=args.loops,
                gain=args.gain,
            )
    except ConversionError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
