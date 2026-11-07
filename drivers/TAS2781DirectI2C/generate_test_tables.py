"""Build the fixed, locally calibrated TAS2781 test tables."""
import plistlib
import struct
import subprocess
import zlib

from build_profile_plan import ROOT, PROFILE, make_plan

KEY = "1f52d2a1-bb3a-457d-bc09-43a3f4310a92:CALI_DATA"
ALIASES = (b"iks_E0", b"yep_LsqM0", b"oyz_U0_ujx", b"umg_SsmKEGCye", b"iks_GC_GMgq")
PLAYBACK_ORDER = (0, 2, 1, 3, 4)


def calibration():
    data = plistlib.loads(subprocess.check_output(["/usr/sbin/nvram", "-x", KEY]))[KEY]
    if not isinstance(data, bytes) or len(data) < 16:
        raise ValueError("missing factory calibration")
    chip, count = struct.unpack_from("<II", data)
    crc_offset = 12 + 24 * count
    if chip != 2781 or count != 3 or crc_offset + 4 > len(data):
        raise ValueError("unexpected factory calibration format")
    if zlib.crc32(data[:crc_offset]) != struct.unpack_from("<I", data, crc_offset)[0]:
        raise ValueError("factory calibration CRC mismatch")

    groups = {}
    for offset in range(12, crc_offset, 24):
        tag, *values = struct.unpack_from("<6I", data, offset)
        if tag in groups:
            raise ValueError("duplicate calibration record")
        groups[tag] = values
    if set(groups) != {0, 1, 0x80} or not all(all(groups[ch]) for ch in (0, 1)):
        raise ValueError("incomplete calibration records")

    trailer = (ROOT / "firmware" / PROFILE).read_bytes()[-480:]
    registers = {}
    for index in range(0, len(trailer), 24):
        entry = trailer[index:index + 24]
        if entry[23] == 1:
            registers[entry[:20].split(b"\0")[0]] = tuple(entry[20:23])
    addresses = [tuple(word.to_bytes(4, "little")[1:]) for word in groups[0x80]]
    if addresses != [registers[alias] for alias in ALIASES]:
        raise ValueError("factory calibration addresses do not match the G835 firmware")
    return [
        (channel, *addresses[index], struct.pack(">I", groups[channel][index]))
        for channel in (0, 1) for index in PLAYBACK_ORDER
    ]


def array(name, writes):
    pool = bytearray()
    lines = []
    for channel, book, page, reg, data in writes:
        if channel not in (0, 1) or not 1 <= reg <= 127 or not 1 <= len(data) <= 120 or reg + len(data) > 128:
            raise ValueError("unsafe register write")
        if page == 0 and reg + len(data) > 127:
            raise ValueError("write overlaps book select")
        lines.append(f"    {{{len(pool)}, {channel}, {book}, {page}, {reg}, {len(data)}}},")
        pool.extend(data)
    rows = ["    " + ", ".join(f"0x{b:02x}" for b in pool[i:i + 20]) + "," for i in range(0, len(pool), 20)]
    return f"static constexpr UInt8 {name}Data[] = {{\n" + "\n".join(rows) + "\n};\n" + \
        f"static constexpr RegisterWrite {name}Writes[] = {{\n" + "\n".join(lines) + "\n};\n"


def generate():
    phases = make_plan()["phases"]
    header = ["// Generated for the local G835 speaker-ID 0 test.\n"]
    for phase in ("dsp_program", "playback_config", "pre_power_up", "pre_shutdown"):
        writes = []
        for block in phases[phase]:
            channel = {0x38: 0, 0x39: 1}[block["address"]]
            for op, book, page, reg, hex_data in block["commands"]:
                if op != "write":
                    raise ValueError(f"unsupported operation in {phase}")
                data = bytes.fromhex(hex_data)
                if phase in ("dsp_program", "playback_config") and book == page == 0 and reg <= 2 < reg + len(data):
                    raise ValueError("firmware programming would touch the power register")
                writes.append((channel, book, page, reg, data))
        header.append(array(phase, writes))
    header.append(array("factory", calibration()))
    return "\n".join(header)


if __name__ == "__main__":
    target = ROOT / "TAS2781TestTables.hpp"
    target.write_text(generate())
    target.chmod(0o600)
    print("Profile 0, two CRC-verified factory records, and matched calibration addresses.")
