# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
# See LICENSE.md for applicable additional terms and warranty disclaimers.
"""Generate independent synthetic SHP/HD masters and a proof MIX; never touches Run."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

import hdpack


def artwork(width, height, phase=0, shadow_only=False):
    color, remap, shadow = bytearray(width * height * 4), bytearray(width * height), bytearray(width * height)
    for y in range(height):
        for x in range(width):
            px, py = x / width, y / height
            body = .32 < px < .68 and .25 < py < .77
            head = (px - .5) ** 2 + (py - .24) ** 2 < .014
            ground = ((px - .53) / .31) ** 2 + ((py - .80) / .09) ** 2 < 1
            p = y * width + x
            rgb, alpha = (0, 0, 0), 0
            if ground:
                alpha, shadow[p] = 190, 255
            if (body or head) and not shadow_only:
                rgb, alpha, remap[p], shadow[p] = (210, 80, 45), 255, 1 + (x + y) % 16, 0
                if (x + phase) % 7 == 0:
                    rgb, remap[p] = (40, 235, 250), 0
                if .30 < py < .36:
                    rgb, remap[p] = (230, 250, 255), 0
            if not shadow_only and .27 < px < .32 and .40 < py < .68:
                rgb, alpha, remap[p], shadow[p] = (50, 220, 255), 100, 0, 0
            color[p * 4:p * 4 + 4] = bytes((*rgb, alpha))
    return bytes(color), bytes(remap), bytes(shadow)


def classic_shp(width, height, count):
    header = bytearray(struct.pack("<hhhh", 0, width, height, count) + bytes(count * 24))
    for frame in range(count):
        color, remap, shadow = artwork(width, height, frame % 8, count > 1 and frame >= count // 2)
        pixels = bytes(0 if color[p * 4 + 3] < 128 else 1 if shadow[p] else 15 + remap[p] if remap[p] else 200 for p in range(width * height))
        record = struct.pack("<hhhhhh3s5sI", 0, 0, width, height, 1, len(pixels), b"\xd0\x90\x30", bytes(5), len(header))
        header[8 + frame * 24:8 + (frame + 1) * 24] = record
        header.extend(pixels)
    return bytes(header)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--validator", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    members, identities = {}, {}
    for name, width, height, count, kind in (("HDTEST.SHP", 48, 48, 512, "shape"), ("HDBUTTON.SHP", 60, 48, 1, "ui")):
        folder = args.output / name.split('.')[0]
        folder.mkdir(exist_ok=True)
        classic = classic_shp(width, height, count)
        (folder / name).write_bytes(classic)
        frames = []
        for frame in range(count):
            key = frame % 8 + (8 if count > 1 and frame >= count // 2 else 0)
            if frame < 8 or count > 1 and count // 2 <= frame < count // 2 + 8:
                color, remap, shadow = artwork(width * 2, height * 2, frame % 8, count > 1 and frame >= count // 2)
                for channel, plane, channels in (("color", color, 4), ("remap", remap, 1), ("shadow", shadow, 1)):
                    (folder / f"{key}-{channel}.png").write_bytes(hdpack.png_write(width * 2, height * 2, plane, channels))
            frames.append({"logical": frame, "image": f"{key}-color.png", "remap": f"{key}-remap.png", "shadow": f"{key}-shadow.png"})
        manifest = {"version": 1, "name": name, "classic": name, "kind": kind, "variants": [{"scale": 2, "logical_width": width, "logical_height": height, "logical_frames": count, "frames": frames}]}
        path = folder / "pack.json"
        path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        pack, _ = hdpack.load_manifest(path)
        hdpack.validate_classic_pair(pack, classic)
        wire = hdpack.encode(pack)
        output = folder / (name + ".HDP")
        output.write_bytes(wire)
        subprocess.run([str(args.validator.resolve()), "--validate", str(output.resolve())], check=True)
        hdpack.export({**pack, "variants": [{**pack["variants"][0], "frames": pack["variants"][0]["frames"][:8]}]}, folder / "preview")
        members[name], members[name + ".HDP"] = classic, wire
        identities[name] = {"classic_sha256": hashlib.sha256(classic).hexdigest(), "hdp_sha256": hashlib.sha256(wire).hexdigest(), "logical_width": width, "logical_height": height, "logical_frames": count}
    mix = hdpack.mix_write(members)
    (args.output / "ecache98.mix").write_bytes(mix)
    identities["ecache98.mix"] = {"sha256": hashlib.sha256(mix).hexdigest(), "bytes": len(mix)}
    (args.output / "identities.json").write_text(json.dumps(identities, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(identities, indent=2))


if __name__ == "__main__":
    main()
