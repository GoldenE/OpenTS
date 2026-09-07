# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
# See LICENSE.md for applicable additional terms and warranty disclaimers.
"""Four-frame, K4 forward/reverse synthetic animation proof."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import hdpack


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--validator", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    images = []
    for phase in range(16):
        rgba, remap = bytearray(64 * 64 * 4), bytearray(64 * 64)
        for y in range(12, 52):
            for x in range(8, 56):
                pixel = y * 64 + x
                bright = (x + phase) % 16 < 4
                rgba[pixel * 4:pixel * 4 + 4] = bytes((40, 240, 255, 255) if bright else (230, 90, 35, 255))
                remap[pixel] = 0 if bright else 8
        images.append((bytes(rgba), bytes(remap)))
        (args.output / f"phase-{phase}.png").write_bytes(hdpack.png_write(64, 64, rgba))
        (args.output / f"remap-{phase}.png").write_bytes(hdpack.png_write(64, 64, remap, 1))
    classic = bytearray(struct.pack("<hhhh", 0, 32, 32, 4) + bytes(4 * 24))
    for frame in range(4):
        rgba, remap = images[frame * 4]
        pixels = bytearray()
        for y in range(32):
            for x in range(32):
                p = (y * 2 + 1) * 64 + x * 2 + 1
                pixels.append(0 if not rgba[p * 4 + 3] else 15 + remap[p] if remap[p] else 200)
        classic[8 + frame * 24:8 + (frame + 1) * 24] = struct.pack("<hhhhhh3s5sI", 0, 0, 32, 32, 1, 1024, b"\x40\xe0\xff", bytes(5), len(classic))
        classic.extend(pixels)
    (args.output / "HDTEMP.SHP").write_bytes(classic)
    frames = []
    for logical in range(4):
        for direction in (1, -1):
            for subframe in range(4):
                phase = (logical * 4 + direction * subframe) % 16
                frames.append({"logical": logical, "direction": direction, "subframe": subframe, "image": f"phase-{phase}.png", "remap": f"remap-{phase}.png"})
    manifest = {"version": 1, "name": "HDTEMP.SHP", "classic": "HDTEMP.SHP", "kind": "shape", "variants": [{"scale": 2, "logical_width": 32, "logical_height": 32, "logical_frames": 4, "sequences": [{"first": 0, "count": 4, "temporal": 4, "reverse": True}], "frames": frames}]}
    path = args.output / "pack.json"
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    pack, _ = hdpack.load_manifest(path)
    hdpack.validate_classic_pair(pack, classic)
    wire = hdpack.encode(pack)
    output = args.output / "HDTEMP.SHP.HDP"
    output.write_bytes(wire)
    subprocess.run([str(args.validator.resolve()), "--validate", str(output.resolve())], check=True)
    hdpack.export(pack, args.output / "preview")
    mix = hdpack.mix_write({"HDTEMP.SHP": bytes(classic), "HDTEMP.SHP.HDP": wire})
    (args.output / "ecache97.mix").write_bytes(mix)
    print(json.dumps({"name": "HDTEMP.SHP", "logical_frames": 4, "temporal": 4, "reverse": True, "mix_sha256": hashlib.sha256(mix).hexdigest(), "mix_bytes": len(mix)}, sort_keys=True))


if __name__ == "__main__":
    main()
