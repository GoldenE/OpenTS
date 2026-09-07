#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
# See LICENSE.md for applicable additional terms and warranty disclaimers.
"""Deterministic HDP v1 pack authoring with Python's standard library."""
import argparse
import binascii
import hashlib
import html
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import zlib

MAX_BYTES = 256 * 1024 * 1024
KINDS = ("shape", "ui", "terrain", "voxel", "palette", "font", "movie")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path, limit=MAX_BYTES):
    path = Path(path)
    require(path.stat().st_size <= limit, f"Input exceeds {limit} bytes: {path}")
    return path.read_bytes()


def integer(value, low, high, label):
    require(type(value) is int and low <= value <= high, f"Invalid {label}: {value!r}")
    return value


def canonical(name):
    name = name.upper()
    require(re.fullmatch(r"[A-Z0-9_-][A-Z0-9_.-]{0,127}", name) and ".." not in name, "Unsafe asset name")
    return name


def png_write(width, height, pixels, channels=4):
    require(channels in (1, 4) and len(pixels) == width * height * channels, "PNG plane size differs")
    def chunk(kind, value):
        return struct.pack(">I", len(value)) + kind + value + struct.pack(">I", binascii.crc32(kind + value) & 0xffffffff)
    rows = b"".join(b"\0" + pixels[y * width * channels:(y + 1) * width * channels] for y in range(height))
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6 if channels == 4 else 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")


def png_read(path):
    data = read(path)
    require(data[:8] == b"\x89PNG\r\n\x1a\n", f"Expected lossless PNG: {path}")
    offset, image, header, ended = 8, bytearray(), None, False
    while offset < len(data):
        require(offset + 12 <= len(data), "Truncated PNG chunk")
        length = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        require(length <= MAX_BYTES and offset + 12 + length <= len(data), "PNG chunk exceeds bounds")
        value = data[offset + 8:offset + 8 + length]
        require(binascii.crc32(kind + value) & 0xffffffff == struct.unpack_from(">I", data, offset + 8 + length)[0], "PNG CRC differs")
        if kind == b"IHDR":
            require(header is None and offset == 8 and length == 13, "Invalid PNG header")
            width, height, bits, color, comp, filt, interlace = struct.unpack(">IIBBBBB", value)
            require(1 <= width <= 4096 and 1 <= height <= 4096 and bits == 8 and color in (0, 2, 6) and comp == filt == interlace == 0, "PNG requires noninterlaced 8-bit grayscale, RGB or RGBA, at most 4096 square")
            header = width, height, {0: 1, 2: 3, 6: 4}[color]
        elif kind == b"IDAT":
            require(header is not None, "PNG data precedes header")
            image.extend(value)
        elif kind == b"IEND":
            require(length == 0 and offset + 12 == len(data), "Invalid PNG end")
            ended = True
        elif kind == b"tRNS":
            raise ValueError("Convert PNG color-key transparency to RGBA before import")
        elif not kind[0] & 32:
            raise ValueError(f"Unsupported PNG critical chunk: {kind!r}")
        offset += length + 12
    require(header and ended, "Incomplete PNG")
    width, height, channels = header
    stride, expected = width * channels, (width * channels + 1) * height
    decoder = zlib.decompressobj()
    raw = decoder.decompress(image, expected + 1)
    require(len(raw) == expected and decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail, "PNG inflated size differs")
    pixels, previous = bytearray(), bytearray(stride)
    for y in range(height):
        method = raw[y * (stride + 1)]
        require(method <= 4, "Invalid PNG filter")
        row = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            left, above, corner = row[x - channels] if x >= channels else 0, previous[x], previous[x - channels] if x >= channels else 0
            prediction = 0
            if method == 1:
                prediction = left
            elif method == 2:
                prediction = above
            elif method == 3:
                prediction = (left + above) // 2
            elif method == 4:
                p = left + above - corner
                distances = abs(p - left), abs(p - above), abs(p - corner)
                prediction = (left, above, corner)[distances.index(min(distances))]
            row[x] = (row[x] + prediction) & 255
        pixels.extend(row)
        previous = row
    return width, height, bytes(pixels), channels


def validate(pack):
    require(pack["name"] == canonical(pack["name"]), "Name must be canonical")
    require(pack["kind"] in KINDS and len(pack["digest"]) == 32 and any(pack["digest"]), "Invalid kind or classic digest")
    require(1 <= len(pack["variants"]) <= 16, "Invalid variant count")
    scales, total = set(), 0
    for v in pack["variants"]:
        scale = integer(v["scale"], 1, 8, "scale")
        require(scale not in scales, "Duplicate scale")
        scales.add(scale)
        integer(v["logical_width"], 1, 4096 // scale, "logical width")
        integer(v["logical_height"], 1, 4096 // scale, "logical height")
        count = integer(v["logical_frames"], 1, 16384, "logical frames")
        facings = integer(v["facings"], 1, 256, "facings")
        require(len(v["sequences"]) <= 16384, "Too many sequences")
        temporal, reverse, assigned = [1] * count, [False] * count, set()
        for s in v["sequences"]:
            first = integer(s["first"], 0, count - 1, "sequence first")
            size = integer(s["count"], 1, count - first, "sequence count")
            multiplier = integer(s["temporal"], 1, 32, "temporal multiplier")
            require(type(s["reverse"]) is bool, "Reverse must be boolean")
            for i in range(first, first + size):
                require(i not in assigned, "Overlapping sequences")
                assigned.add(i)
                temporal[i], reverse[i] = multiplier, s["reverse"]
        keys = set()
        total += len(v["frames"])
        require(total <= 16384, "Too many total frames")
        for f in v["frames"]:
            logical = integer(f["logical"], 0, count - 1, "logical frame")
            integer(f["subframe"], 0, temporal[logical] - 1, "subframe")
            integer(f["facing"], 0, facings - 1, "facing")
            require(f["direction"] in (1, -1) and (f["direction"] == 1 or reverse[logical]), "Invalid direction")
            key = logical, f["subframe"], f["facing"], f["direction"]
            require(key not in keys, "Duplicate frame identity")
            keys.add(key)
            integer(f["x"], -4096, 4096, "x offset")
            integer(f["y"], -4096, 4096, "y offset")
            n = integer(f["width"], 1, 4096, "width") * integer(f["height"], 1, 4096, "height")
            require(len(f["color"]) == n * 4 and len(f["remap"]) in (0, n) and len(f["shadow"]) in (0, n) and len(f["depth"]) in (0, n * 2), "Frame planes differ")
            require(not f["remap"] or max(f["remap"]) <= 16, "Remap must be 0 or house shade 1..16")
            require(pack["kind"] != "terrain" or len(f["depth"]) == n * 2, "Terrain requires depth")
        for logical in {key[0] for key in keys}:
            for facing in range(facings):
                for subframe in range(temporal[logical]):
                    for direction in ((1, -1) if reverse[logical] else (1,)):
                        require((logical, subframe, facing, direction) in keys, "Incomplete temporal/facing block")
        require(len(v["model"]) == 9, "Model requires nine signed 16.16 values")
        for value in v["model"]:
            integer(value, -2147483648, 2147483647, "model component")
        if pack["kind"] == "voxel":
            require(v["voxel"] and not v["frames"] and not v["motion"] and not v["sequences"], "Voxel requires VXL only; classic HVA remains authoritative")
            require(all(v["model"][i] < v["model"][i + 3] for i in range(3)), "Invalid model bounds")
        else:
            require(v["frames"] and not v["voxel"] and not v["motion"] and not any(v["model"]), "Raster variant contains model data")


def encode(pack):
    validate(pack)
    out = bytearray()
    def u32(*values):
        out.extend(struct.pack("<" + "I" * len(values), *(x & 0xffffffff for x in values)))
    def blob(value):
        require(len(value) <= MAX_BYTES, "Plane exceeds limit")
        u32(len(value))
        out.extend(value)
        require(len(out) <= MAX_BYTES, "Encoded pack exceeds limit")
    u32(0x5048444f, 1, KINDS.index(pack["kind"]))
    blob(pack["name"].encode("ascii"))
    blob(pack["digest"])
    u32(len(pack["variants"]))
    for v in pack["variants"]:
        u32(*(v[key] for key in ("scale", "logical_width", "logical_height", "logical_frames", "facings")), *v["model"])
        blob(v["voxel"])
        blob(v["motion"])
        u32(len(v["sequences"]))
        for s in v["sequences"]:
            u32(s["first"], s["count"], s["temporal"], int(s["reverse"]))
        u32(len(v["frames"]))
        for f in v["frames"]:
            u32(*(f[key] for key in ("logical", "subframe", "facing", "direction", "x", "y", "width", "height")))
            for key in ("color", "remap", "shadow", "depth"):
                blob(f[key])
    return bytes(out)


def load_manifest(path):
    path = Path(path)
    manifest = json.loads(read(path, 4 * 1024 * 1024))
    require(manifest.get("version") == 1, "Manifest version must be 1")
    classic = read(path.parent / manifest["classic"])
    pack = {"name": canonical(manifest["name"]), "kind": manifest.get("kind", "shape"), "digest": hashlib.sha256(classic).digest(), "variants": []}
    require(1 <= len(manifest["variants"]) <= 16, "Invalid variant count")
    allocated = 0
    images = {}
    image_bytes = 0
    def image(relative):
        nonlocal image_bytes
        image_path = (path.parent / relative).resolve()
        if image_path not in images:
            value = png_read(image_path)
            image_bytes += len(value[2])
            require(image_bytes <= MAX_BYTES, "Source image cache exceeds budget")
            images[image_path] = value
        return images[image_path]
    for source in sorted(manifest["variants"], key=lambda v: v["scale"]):
        v = {key: source[key] for key in ("scale", "logical_width", "logical_height", "logical_frames")}
        v.update(facings=source.get("facings", 1), model=source.get("model", [0] * 9), voxel=b"", motion=b"", sequences=[], frames=[])
        if "voxel" in source:
            v["voxel"] = read(path.parent / source["voxel"])
        for sequence in source.get("sequences", []):
            v["sequences"].append(dict(first=sequence["first"], count=sequence["count"], temporal=sequence.get("temporal", 1), reverse=sequence.get("reverse", False)))
        v["sequences"].sort(key=lambda s: s["first"])
        require(len(source.get("frames", [])) <= 16384, "Too many frames")
        for record in source.get("frames", []):
            width, height, pixels, channels = image(record["image"])
            if channels != 4:
                pixels = b"".join((pixels[i:i + 3] if channels == 3 else pixels[i:i + 1] * 3) + b"\xff" for i in range(0, len(pixels), channels))
            f = {key: record.get(key, default) for key, default in (("logical", 0), ("subframe", 0), ("facing", 0), ("direction", 1), ("x", 0), ("y", 0))}
            f.update(width=width, height=height, color=pixels, remap=b"", shadow=b"", depth=b"")
            for mask in ("remap", "shadow"):
                if mask in record:
                    mw, mh, plane, mc = image(record[mask])
                    require((mw, mh, mc) == (width, height, 1), "Masks require same-sized grayscale PNG")
                    f[mask] = plane
            if "depth" in record:
                f["depth"] = read(path.parent / record["depth"])
            allocated += sum(len(f[key]) for key in ("color", "remap", "shadow", "depth")) + 256
            require(allocated <= MAX_BYTES, "Decoded pack exceeds budget")
            v["frames"].append(f)
        v["frames"].sort(key=lambda f: (f["logical"], f["subframe"], f["facing"], f["direction"]))
        pack["variants"].append(v)
    validate(pack)
    return pack, classic


def export(pack, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    cards = []
    for v in pack["variants"]:
        for f in v["frames"]:
            stem = f"s{v['scale']}-f{f['logical']}-t{f['subframe']}-a{f['facing']}-d{f['direction']}"
            (directory / (stem + ".png")).write_bytes(png_write(f["width"], f["height"], f["color"]))
            images = [f'<img alt="RGBA" src="{stem}.png">']
            for mask in ("remap", "shadow"):
                if f[mask]:
                    (directory / f"{stem}-{mask}.png").write_bytes(png_write(f["width"], f["height"], f[mask], 1))
                    images.append(f'<img alt="{mask}" title="{mask}" src="{stem}-{mask}.png">')
            if f["depth"]:
                (directory / f"{stem}-depth.i16le").write_bytes(f["depth"])
            cards.append(f'<figure data-frame="{f["logical"]}"><figcaption>{stem}: offset ({f["x"]},{f["y"]}), logical canvas {v["logical_width"]}×{v["logical_height"]}</figcaption>{"".join(images)}</figure>')
    document = '<!doctype html><meta charset="utf-8"><title>HD pack review</title><style>body{font:16px system-ui;background:#222;color:white}figure{display:inline-block;margin:16px;padding:12px;border:1px solid #888}img{image-rendering:pixelated;max-width:512px;background:repeating-conic-gradient(#777 0% 25%,#aaa 0% 50%) 0 0/16px 16px}figcaption{margin-bottom:10px}</style>'
    document += f'<h1>{html.escape(pack["name"])}</h1><p>RGBA, semantic remap and shadow planes. Frame offsets are physical pixels from the logical canvas origin. Review sequence order, facings, anchors, shadow consistency and terrain seams before shipping.</p>'
    document += ''.join(cards)
    (directory / "index.html").write_text(document, encoding="utf-8", newline="\n")


def legacy_shp(pack, classic, palette):
    require(pack["kind"] in ("shape", "ui", "font"), "Legacy SHP export requires a shape variant")
    require(len(palette) == 768 and max(palette) <= 63, "Legacy palette must be 768 six-bit RGB bytes")
    require(len(classic) >= 8, "Truncated classic SHP")
    flags, width, height, count = struct.unpack_from("<hhhh", classic)
    require(0 < width <= 4096 and 0 < height <= 4096 and 0 < count <= 16384 and 8 + count * 24 <= len(classic), "Invalid classic SHP header")
    variant = min(pack["variants"], key=lambda v: v["scale"])
    require((width, height, count) == (variant["logical_width"], variant["logical_height"], variant["logical_frames"]), "SHP logical metadata differs")
    frames = {f["logical"]: f for f in variant["frames"] if f["subframe"] == f["facing"] == 0 and f["direction"] == 1}
    colors = [tuple(palette[i * 3 + c] * 4 for c in range(3)) for i in range(256)]
    out = bytearray(classic)
    report = []
    for logical in range(count):
        if logical not in frames:
            report.append(f"frame {logical}: preserved classic bytes (no HD base frame)")
            continue
        f = frames[logical]
        record = 8 + logical * 24
        crop_x, crop_y, crop_width, crop_height, frame_flags = struct.unpack_from("<hhhhh", classic, record)
        require(crop_width >= 0 and crop_height >= 0, "Invalid classic SHP frame crop")
        if not crop_width or not crop_height:
            report.append(f"frame {logical}: preserved empty classic crop and bytes")
            continue
        require(crop_width * crop_height <= MAX_BYTES, "Legacy SHP frame exceeds decode budget")
        if not frame_flags & 2:
            require(crop_width * crop_height <= 32767, "Uncompressed legacy SHP frame exceeds signed Size field")
        pixels = bytearray(crop_width * crop_height)
        quantized, alpha_loss = 0, 0
        for y in range(crop_height):
            for x in range(crop_width):
                sx = (crop_x + x) * variant["scale"] + variant["scale"] // 2 - f["x"]
                sy = (crop_y + y) * variant["scale"] + variant["scale"] // 2 - f["y"]
                if not (0 <= sx < f["width"] and 0 <= sy < f["height"]):
                    continue
                p = sy * f["width"] + sx
                alpha = f["color"][p * 4 + 3]
                alpha_loss += alpha not in (0, 255)
                if alpha < 128:
                    continue
                if f["remap"] and f["remap"][p]:
                    index = 15 + f["remap"][p]
                else:
                    rgb = tuple(f["color"][p * 4:p * 4 + 3])
                    index = min((i for i in range(1, 256) if not 16 <= i <= 31), key=lambda i: sum((rgb[c] - colors[i][c]) ** 2 for c in range(3)))
                    quantized += rgb != colors[index]
                pixels[y * crop_width + x] = index
        payload = pixels
        if frame_flags & 2:
            payload = bytearray()
            for y in range(crop_height):
                row, x = bytearray(), 0
                while x < crop_width:
                    value = pixels[y * crop_width + x]
                    if value:
                        row.append(value)
                        x += 1
                    else:
                        run = 1
                        while run < 255 and x + run < crop_width and pixels[y * crop_width + x + run] == 0:
                            run += 1
                        row.extend((0, run))
                        x += run
                require(len(row) + 2 <= 65535, "Legacy SHP RLE row exceeds length field")
                require(len(payload) + len(row) + 2 <= 32767, "Encoded legacy SHP frame exceeds signed Size field")
                payload.extend(struct.pack("<H", len(row) + 2))
                payload.extend(row)
        struct.pack_into("<hh", out, record + 8, frame_flags | 1, len(payload))
        struct.pack_into("<I", out, record + 20, len(out))
        out.extend(payload)
        encoding = "RLE" if frame_flags & 2 else "raw"
        report.append(f"frame {logical}: preserved crop ({crop_x},{crop_y},{crop_width},{crop_height}) and {encoding} encoding; nearest center downsample {variant['scale']}x; {quantized} palette approximations; {alpha_loss} partial-alpha thresholds; shadow/depth planes not encoded")
    report.append("Temporal subframes and extended facings use forward base frame/facing zero; original frame numbering, crop-area ranking and radar colors preserved.")
    return bytes(out), report


def validate_classic_pair(pack, classic):
    """Check contracts that require the actual paired SHP, not just the HDP."""
    diagnostics = []
    if pack["kind"] == "terrain":
        from hdpreview import tmp_records
        records = tmp_records(classic)
        for variant in pack["variants"]:
            scale = variant["scale"]
            require((variant["logical_width"], variant["logical_height"], variant["logical_frames"]) == (48, 24, len(records)), f"Scale {scale}: logical terrain metadata differs from paired TMP; runtime would use classic")
            for frame in variant["frames"]:
                record = records[frame["logical"]]
                require(record is not None, "HD terrain frame targets an absent classic subtile")
                left, top, right, bottom = 0, 0, 48, 24
                if record["flags"] & 1:
                    left, top = min(left, record["ex"]), min(top, record["ey"])
                    right, bottom = max(right, record["ex"] + record["ew"]), max(bottom, record["ey"] + record["eh"])
                require(frame["x"] >= left * scale and frame["y"] >= top * scale and frame["x"] + frame["width"] <= right * scale and frame["y"] + frame["height"] <= bottom * scale,
                        f"Scale {scale}, frame {frame['logical']}: rectangle exceeds paired TMP footprint; runtime would use classic")
            missing = sorted(set(range(len(records))) - {frame["logical"] for frame in variant["frames"]})
            if missing:
                diagnostics.append(f"scale {scale}: absent terrain frames {', '.join(map(str, missing))} use paired classic artwork")
        return diagnostics
    if pack["kind"] == "voxel":
        def topology(data):
            require(len(data) >= 32, "Paired VXL header is truncated")
            palettes, layers, infos, _ = struct.unpack_from("<4I", data, 16)
            start = 32 + palettes * 770
            require(0 < layers <= 64 and layers <= infos <= 64 and start + layers * 28 <= len(data), "Paired VXL topology exceeds supported bounds")
            return layers, infos, tuple(struct.unpack_from("<I", data, start + layer * 28 + 16)[0] for layer in range(layers))
        original = topology(classic)
        for variant in pack["variants"]:
            require(topology(variant["voxel"]) == original, f"Scale {variant['scale']}: VXL layer topology differs from paired classic; runtime would use classic")
        return diagnostics
    if pack["kind"] not in ("shape", "ui", "font") or not pack["name"].endswith(".SHP"):
        diagnostics.append(f"{pack['name']}: version 1 has no HD replacement consumer for this file family; classic artwork remains active")
        return diagnostics
    require(len(classic) >= 8, "Paired SHP header is truncated")
    _, width, height, count = struct.unpack_from("<hhhh", classic)
    require(width > 0 and height > 0 and count > 0 and 8 + count * 24 <= len(classic), "Paired SHP header or frame table is invalid")
    for variant in pack["variants"]:
        scale = variant["scale"]
        require((variant["logical_width"], variant["logical_height"], variant["logical_frames"]) == (width, height, count), f"Scale {scale}: logical width/height/frame count differs from paired SHP; runtime would use classic")
        present = set()
        for frame in variant["frames"]:
            logical = frame["logical"]
            x, y, w, h = struct.unpack_from("<hhhh", classic, 8 + logical * 24)
            identity = f"scale {scale}, frame {logical}, subframe {frame['subframe']}, facing {frame['facing']}, direction {frame['direction']}"
            require(w > 0 and h > 0 and frame["x"] >= x * scale and frame["y"] >= y * scale and frame["x"] + frame["width"] <= (x + w) * scale and frame["y"] + frame["height"] <= (y + h) * scale,
                    f"{identity}: rectangle exceeds paired SHP crop ({x},{y},{w},{h}); runtime would use classic")
            present.add(logical)
        missing = sorted(set(range(count)) - present)
        ranges = []
        while missing:
            first = last = missing[0]
            index = 1
            while index < len(missing) and missing[index] == last + 1:
                last = missing[index]
                index += 1
            ranges.append(str(first) if first == last else f"{first}-{last}")
            missing = missing[index:]
        if ranges:
            diagnostics.append(f"scale {scale}: absent logical frames {', '.join(ranges)} use paired classic artwork")
    return diagnostics


def mix_write(members):
    """Unencrypted MIX with the production CRCEngine's padded CRC32 names."""
    entries, body = [], bytearray()
    require(0 < len(members) <= 32767, "Invalid MIX member count")
    for name in sorted(members):
        name_bytes = canonical(name).encode("ascii")
        remainder = len(name_bytes) % 4
        if remainder:
            name_bytes += bytes([remainder]) + bytes([name_bytes[-remainder]]) * (3 - remainder)
        crc = binascii.crc32(name_bytes) & 0xffffffff
        signed = crc if crc < 0x80000000 else crc - 0x100000000
        entries.append((signed, len(body), len(members[name])))
        body.extend(members[name])
    require(len({entry[0] for entry in entries}) == len(entries), "MIX name hash collision")
    require(len(body) <= 0x7fffffff, "MIX body too large")
    return struct.pack("<hI", len(entries), len(body)) + b"".join(struct.pack("<iII", *entry) for entry in sorted(entries)) + body


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preview", type=Path)
    parser.add_argument("--terrain-layout", type=Path, help="Terrain seam placements JSON; requires --preview and a terrain pack")
    parser.add_argument("--validator", type=Path, help="HDAssetsTest.exe for production decoder validation; required for VXL")
    parser.add_argument("--legacy-shp", type=Path)
    parser.add_argument("--palette", type=Path)
    parser.add_argument("--accept-lossy-legacy", action="store_true")
    parser.add_argument("--mix", type=Path, help="Write an unencrypted MIX containing the matching classic and HDP pair")
    args = parser.parse_args()
    pack, classic = load_manifest(args.manifest)
    require(not args.terrain_layout or args.preview and pack["kind"] == "terrain", "--terrain-layout requires a terrain pack and --preview")
    require(pack["kind"] != "voxel" or args.validator, "VXL authoring requires --validator for native span validation")
    if args.legacy_shp:
        require(args.accept_lossy_legacy and args.palette, "Legacy down-conversion requires --palette and --accept-lossy-legacy")
        shp, report = legacy_shp(pack, classic, read(args.palette, 768))
        pack["digest"] = hashlib.sha256(shp).digest()
    fallbacks = validate_classic_pair(pack, shp if args.legacy_shp else classic)
    wire = encode(pack)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(wire)
    if args.validator:
        subprocess.run([str(args.validator.resolve()), "--validate", str(args.output.resolve())], check=True)
    if args.preview:
        export(pack, args.preview)
        import hdpreview
        preview_links = []
        preview_palette = read(args.palette, 768) if args.palette else None
        if preview_palette is not None and pack["kind"] in ("shape", "ui", "font") and pack["name"].endswith(".SHP"):
            hdpreview.palette_preview(shp if args.legacy_shp else classic, preview_palette, args.preview / "legacy")
            preview_links.append('<p><a href="legacy/index.html">Palette-applied paired SHP and palette swatches</a></p>')
        if pack["kind"] == "terrain":
            layout = json.loads(read(args.terrain_layout, 4 * 1024 * 1024)) if args.terrain_layout else None
            hdpreview.terrain_preview(pack, classic, args.preview / "terrain", layout, preview_palette)
            preview_links.append('<p><a href="terrain/index.html">Assembled terrain seams and coverage</a></p>')
        if preview_links:
            index = args.preview / "index.html"
            index.write_text(index.read_text(encoding="utf-8") + ''.join(preview_links), encoding="utf-8", newline="\n")
    if args.legacy_shp:
        args.legacy_shp.parent.mkdir(parents=True, exist_ok=True)
        args.legacy_shp.write_bytes(shp)
        args.legacy_shp.with_suffix(args.legacy_shp.suffix + ".report.txt").write_text("\n".join(report) + "\n", encoding="utf-8")
    if args.mix:
        args.mix.parent.mkdir(parents=True, exist_ok=True)
        args.mix.write_bytes(mix_write({pack["name"]: shp if args.legacy_shp else classic, pack["name"] + ".HDP": wire}))
    print(json.dumps({"name": pack["name"], "bytes": len(wire), "sha256": hashlib.sha256(wire).hexdigest(), "classic_sha256": pack["digest"].hex(), "variants": len(pack["variants"]), "color_conversion": "none; straight RGBA bytes retained", "fallbacks": fallbacks}, sort_keys=True))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, KeyError, OSError, zlib.error, struct.error, subprocess.CalledProcessError) as error:
        print(f"hdpack: {error}", file=sys.stderr)
        sys.exit(1)
