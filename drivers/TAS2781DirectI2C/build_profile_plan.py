"""Generate the board-specific TAS2781 sequence for offline review only."""
from hashlib import sha256
from pathlib import Path
import json

from inspect_firmware import decode_dsp, decode_rca

ROOT = Path(__file__).resolve().parent
PROFILE = "TAS2XXX3F100.bin"
PROFILE_HASH = "b7c8081289b6f868e5bdf328c008535102abf7f73e98f6d044d94148d287304f"
AMPLIFIERS = (0x38, 0x39)
# Subset of the Linux PPC3 TAS2781 block map for this two-amplifier board.
DSP_TARGETS = {
    0x01: AMPLIFIERS,
    **dict.fromkeys((0x02, 0x12, 0x22, 0x41, 0x45), (0x38,)),
    **dict.fromkeys((0x03, 0x13, 0x23, 0x42, 0x46), (0x39,)),
}
RCA_TARGETS = {0: AMPLIFIERS, 1: (0x38,), 2: (0x39,)}


def expand(blocks, targets, key):
    return [
        {"address": address, "commands": [
            [value.hex() if isinstance(value, bytes) else value for value in command]
            for command in block.commands
        ]}
        for block in blocks
        for address in targets[key(block)]
    ]


def make_plan():
    firmware = (ROOT / "firmware" / PROFILE).read_bytes()
    if sha256(firmware).hexdigest() != PROFILE_HASH:
        raise ValueError("unexpected G835 speaker-ID 0 firmware hash")
    program, playback, calibration = decode_dsp(firmware)
    if program[0] != "Tuning Mode" or "G835_JGX" not in playback[0] or not calibration[0].startswith("calibration"):
        raise ValueError("unexpected DSP program or configuration")
    rca = dict(decode_rca((ROOT / "firmware" / "TIAS2781RCA2.bin").read_bytes()))["dsp-mode"]
    phases = {
        "dsp_program": expand(program[1], DSP_TARGETS, lambda block: block.kind),
        "playback_config": expand(playback[1], DSP_TARGETS, lambda block: block.kind),
        "pre_power_up": expand((b for b in rca if b.kind == 4), RCA_TARGETS, lambda block: block.target),
        "pre_shutdown": expand((b for b in rca if b.kind == 3), RCA_TARGETS, lambda block: block.target),
    }
    for phase, blocks in phases.items():
        if {block["address"] for block in blocks} != set(AMPLIFIERS):
            raise ValueError(f"{phase} must target both individual amplifiers")
    return {"board": "10433F10", "speaker_id": 0, "firmware": PROFILE,
            "firmware_sha256": PROFILE_HASH, "offline_only": True, "phases": phases}


if __name__ == "__main__":
    plan = make_plan()
    (ROOT / "build" / "g835-profile0-plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    for phase, blocks in plan["phases"].items():
        print(phase, sum(len(block["commands"]) for block in blocks), "commands to 0x38/0x39")
