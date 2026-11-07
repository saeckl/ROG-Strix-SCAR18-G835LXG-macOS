"""Decode the G835 TAS2781 firmware into data, without any hardware access."""

from dataclasses import dataclass
from pathlib import Path
import struct
from typing import Optional


class Reader:
    def __init__(self, data: bytes):
        self.data = data
        self.offset = 0

    def take(self, length: int) -> bytes:
        end = self.offset + length
        if end > len(self.data):
            raise ValueError(f"truncated data at {self.offset:#x}")
        data = self.data[self.offset:end]
        self.offset = end
        return data

    def read(self, fmt: str) -> tuple:
        return struct.unpack(fmt, self.take(struct.calcsize(fmt)))

    def end(self) -> None:
        if self.offset != len(self.data):
            raise ValueError(f"trailing bytes at {self.offset:#x}")


@dataclass(frozen=True)
class Block:
    target: Optional[int]
    kind: int
    commands: tuple


def decode_commands(data: bytes, count: int) -> tuple:
    reader = Reader(data)
    commands = []
    for _ in range(count):
        _, opcode, value = reader.read(">BBH")
        if opcode == 1:
            for _ in range(value):
                book, page, register, byte = reader.read(">BBBB")
                commands.append(("write", book, page, register, bytes([byte])))
        elif opcode == 2:
            book, page, register, _ = reader.read(">BBBB")
            if value == 0 or value % 4 or register + value > 128:
                raise ValueError("invalid burst size or register range")
            commands.append(("write", book, page, register, reader.take(value)))
        elif opcode == 3:
            commands.append(("delay_ms", value))
        elif opcode == 4:
            book, page, register, byte = reader.read(">BBBB")
            commands.append(("update_bits", book, page, register, value & 0xff, byte))
        else:
            raise ValueError(f"unsupported TAS2781 opcode {opcode}")
    reader.end()
    return tuple(commands)


def decode_image(reader: Reader, header_size: int) -> tuple:
    name = reader.take(header_size)[:64].split(b"\0", 1)[0].decode("ascii")
    (count,) = reader.read(">I")
    blocks = []
    for _ in range(count):
        kind, _, size, subblocks = reader.read(">I4sII")
        blocks.append(Block(None, kind, decode_commands(reader.take(size), subblocks)))
    return name, tuple(blocks)


def decode_dsp(data: bytes) -> tuple:
    reader = Reader(data)
    magic, size, _, ppc = reader.read(">4sIII")
    reader.take(4)
    (version,) = reader.read(">I")
    reader.take(68)
    family, device, programs = reader.read(">HHI")
    if (magic, size, ppc, version, family, device) != (b"5552", len(data), 0x19500, 0x100, 0, 11):
        raise ValueError("not the expected two-amplifier G835 DSP format")
    program_sizes = reader.read(">5I")
    (configs,) = reader.read(">I")
    config_sizes = reader.read(">10I")
    if not 1 <= programs <= len(program_sizes) or not 1 <= configs <= len(config_sizes):
        raise ValueError("invalid DSP image count")
    images = []
    for sizes, count, header_size in ((program_sizes, programs, 72), (config_sizes, configs, 80)):
        for size in sizes[:count]:
            image = Reader(reader.take(size))
            images.append(decode_image(image, header_size))
            image.end()
    # The 0x19500 format ends with 40 reserved bytes and a 480-byte calibration address table.
    reader.take(520)
    reader.end()
    return tuple(images)


def decode_rca(data: bytes) -> tuple:
    reader = Reader(data)
    size, _, version = reader.read(">III")
    reader.take(11)
    (devices,) = reader.read(">B")
    reader.take(8)
    (count,) = reader.read(">I")
    sizes = reader.read(">64I")
    if size != len(data) or version < 0x105 or devices != 2 or not 1 <= count <= len(sizes):
        raise ValueError("not the expected two-amplifier RCA format")
    profiles = []
    for size in sizes[:count]:
        profile = Reader(reader.take(size))
        name = profile.take(64).split(b"\0", 1)[0].decode("ascii")
        (count,) = profile.read(">I")
        blocks = []
        for _ in range(count):
            target, kind, _, size, subblocks = profile.read(">BBHII")
            if target > 2:
                raise ValueError(f"unexpected RCA target {target}")
            blocks.append(Block(target, kind, decode_commands(profile.take(size), subblocks)))
        profile.end()
        profiles.append((name, tuple(blocks)))
    reader.end()
    return tuple(profiles)


if __name__ == "__main__":
    firmware_dir = Path(__file__).resolve().parent
    for name, decode in (("TAS2XXX3F100.bin", decode_dsp), ("TAS2XXX3F101.bin", decode_dsp),
                         ("TIAS2781RCA2.bin", decode_rca)):
        images = decode((firmware_dir / name).read_bytes())
        for title, blocks in images:
            print(f"{name}: {title}, {len(blocks)} blocks, "
                  f"{sum(len(block.commands) for block in blocks)} decoded commands")
