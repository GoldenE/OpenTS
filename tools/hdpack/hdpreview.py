# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
# See LICENSE.md for applicable additional terms and warranty disclaimers.
"""Bounded palette and terrain authoring previews; no proprietary dependencies."""
from array import array
import html
import json
from pathlib import Path
import struct

import hdpack


def colors(palette):
    hdpack.require(len(palette) == 768 and max(palette) <= 63, "Preview palette must contain 768 six-bit RGB bytes")
    return [bytes(palette[i * 3 + c] * 4 for c in range(3)) for i in range(256)]


def decode_shp(data):
    hdpack.require(len(data) >= 8, "Truncated SHP header")
    _, width, height, count = struct.unpack_from("<hhhh", data)
    hdpack.require(0 < width <= 4096 and 0 < height <= 4096 and 0 < count <= 16384 and 8 + count * 24 <= len(data), "Invalid SHP preview header")
    records = [struct.unpack_from("<hhhhhh3s5sI", data, 8 + i * 24) for i in range(count)]
    offsets = sorted({record[8] for record in records if record[8]} | {len(data)})
    frames, total = [], 0
    for record in records:
        x, y, w, h, flags, _, _, _, offset = record
        if not offset or not w or not h:
            frames.append((x, y, 0, 0, b""))
            continue
        hdpack.require(w > 0 and h > 0 and w <= 4096 and h <= 4096 and offset >= 8 + count * 24 and offset < len(data), "Invalid SHP frame rectangle or offset")
        total += w * h
        hdpack.require(total <= hdpack.MAX_BYTES, "SHP preview decode budget exceeded")
        end = next(value for value in offsets if value > offset)
        if flags & 2:
            pixels, position = bytearray(), offset
            for row in range(h):
                hdpack.require(position + 2 <= end, "Truncated SHP row")
                length = struct.unpack_from("<H", data, position)[0]
                hdpack.require(length >= 2 and position + length <= end, "SHP row crosses frame boundary")
                finish, cursor, decoded = position + length, position + 2, bytearray()
                while cursor < finish and len(decoded) < w:
                    value = data[cursor]
                    cursor += 1
                    if value:
                        decoded.append(value)
                    else:
                        hdpack.require(cursor < finish and data[cursor] > 0 and len(decoded) + data[cursor] <= w, "Invalid SHP transparent run")
                        decoded.extend(bytes(data[cursor]))
                        cursor += 1
                hdpack.require(len(decoded) == w and cursor == finish, "SHP row decoded size differs")
                pixels.extend(decoded)
                position = finish
        else:
            hdpack.require(offset + w * h <= end, "Truncated SHP pixels")
            pixels = data[offset:offset + w * h]
        frames.append((x, y, w, h, bytes(pixels)))
    return width, height, frames


def palette_preview(classic, palette, directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    lookup = colors(palette)
    width, height, frames = decode_shp(classic)
    left = min([0] + [f[0] for f in frames])
    top = min([0] + [f[1] for f in frames])
    right = max([width] + [f[0] + f[2] for f in frames])
    bottom = max([height] + [f[1] + f[3] for f in frames])
    w, h = right - left, bottom - top
    hdpack.require(0 < w <= 4096 and 0 < h <= 4096 and w * h * len(frames) * 4 <= hdpack.MAX_BYTES, "Palette preview canvas/export budget exceeded")
    cards = []
    for number, (x, y, fw, fh, indices) in enumerate(frames):
        rgba = bytearray(w * h * 4)
        for row in range(fh):
            for col in range(fw):
                index = indices[row * fw + col]
                if index:
                    offset = ((y - top + row) * w + x - left + col) * 4
                    rgba[offset:offset + 4] = lookup[index] + b"\xff"
        name = f"fallback-{number}.png"
        (directory / name).write_bytes(hdpack.png_write(w, h, rgba))
        cards.append(f'<figure><figcaption>Logical frame {number}; crop ({x},{y},{fw},{fh})</figcaption><img src="{name}"></figure>')
    swatch = bytearray(128 * 128 * 4)
    for y in range(128):
        for x in range(128):
            swatch[(y * 128 + x) * 4:(y * 128 + x + 1) * 4] = lookup[(y // 8) * 16 + x // 8] + b"\xff"
    (directory / "palette.png").write_bytes(hdpack.png_write(128, 128, swatch))
    page = '<!doctype html><meta charset="utf-8"><title>Palette fallback preview</title><style>body{font:16px system-ui;background:#222;color:white}figure{display:inline-block;margin:12px}img{image-rendering:pixelated;background:repeating-conic-gradient(#777 0% 25%,#aaa 0% 50%) 0 0/16px 16px}</style>'
    page += f'<h1>Paired SHP with supplied palette</h1><p>Palette indices run left to right, top to bottom. Index 0 is transparent. Logical canvas {width}×{height}; common preview origin ({left},{top}). These are actual decoded fallback frames, including retained compressed frames.</p><img src="palette.png">' + ''.join(cards)
    (directory / "index.html").write_text(page, encoding="utf-8", newline="\n")


def tmp_records(data):
    hdpack.require(len(data) >= 16, "Truncated TMP header")
    columns, rows, width, height = struct.unpack_from("<iiii", data)
    hdpack.require(columns > 0 and rows > 0 and columns * rows <= 16384 and (width, height) == (48, 24), "Terrain preview requires a bounded 48 by 24 TMP set")
    count = columns * rows
    hdpack.require(16 + count * 4 <= len(data), "Truncated TMP table")
    records = []
    for index in range(count):
        offset = struct.unpack_from("<I", data, 16 + index * 4)[0]
        if not offset:
            records.append(None)
            continue
        hdpack.require(offset >= 16 + count * 4 and offset + 52 + 576 <= len(data), "TMP record/pixels outside file")
        x, y, extra, depth, extra_depth, ex, ey, ew, eh, flags = struct.unpack_from("<9iI", data, offset)
        if flags & 2:
            hdpack.require(depth >= 52 and offset + depth + 576 <= len(data), "TMP depth outside file")
        if flags & 1:
            hdpack.require(0 < ew <= 4096 and 0 < eh <= 4096 and extra >= 52 and offset + extra + ew * eh <= len(data), "TMP extra image outside file")
            if flags & 2:
                hdpack.require(extra_depth >= 52 and offset + extra_depth + ew * eh <= len(data), "TMP extra depth outside file")
        records.append(dict(offset=offset, x=x, y=y, extra=extra, depth=depth, extra_depth=extra_depth, ex=ex - x, ey=ey - y, ew=ew, eh=eh, flags=flags))
    return records


def classic_tile_sample(data, record, x, y):
    sample = None
    if 0 <= y < 23:
        half = y if y < 12 else 22 - y
        first, count = 22 - 2 * half, 4 + 4 * half
        if first <= x < first + count:
            row = 2 * y * (y + 1) if y < 12 else 576 - 2 * (23 - y) * (24 - y)
            position = row + x - first
            index = data[record["offset"] + 52 + position]
            depth = data[record["offset"] + record["depth"] + position] if record["flags"] & 2 else 0
            sample = index, depth, True
    if record["flags"] & 1 and record["ex"] <= x < record["ex"] + record["ew"] and record["ey"] <= y < record["ey"] + record["eh"]:
        position = (y - record["ey"]) * record["ew"] + x - record["ex"]
        index = data[record["offset"] + record["extra"] + position]
        depth = data[record["offset"] + record["extra_depth"] + position] if record["flags"] & 2 else 0
        if index:
            sample = index, depth, True
        elif sample is None:
            sample = 0, depth, False
    return sample


def assemble_terrain(pack, classic, layout=None, palette=None):
    hdpack.require(pack["kind"] == "terrain", "Seam preview requires a terrain pack")
    records = tmp_records(classic)
    layout = layout or {"placements": [{"frame": i % len(records), "cell_x": x, "cell_y": y} for i, (x, y) in enumerate(((0, 0), (1, 0), (0, 1), (1, 1)))]}
    hdpack.require(0 < len(layout["placements"]) <= 256, "Terrain layout requires 1 through 256 placements")
    scale = layout.get("scale", min(v["scale"] for v in pack["variants"]))
    variant = next((v for v in pack["variants"] if v["scale"] == scale), None)
    hdpack.require(variant is not None and (variant["logical_width"], variant["logical_height"], variant["logical_frames"]) == (48, 24, len(records)), "Terrain variant does not match TMP header")
    frames = {f["logical"]: f for f in variant["frames"] if f["subframe"] == f["facing"] == 0 and f["direction"] == 1}
    lookup = colors(palette) if palette is not None else None
    placements, work, fallbacks = [], 0, []
    for item in layout["placements"]:
        frame = hdpack.integer(item["frame"], 0, len(records) - 1, "terrain frame")
        cx = hdpack.integer(item["cell_x"], -256, 256, "cell x")
        cy = hdpack.integer(item["cell_y"], -256, 256, "cell y")
        record = records[frame]
        hdpack.require(record is not None, "Terrain layout references an absent classic record")
        x, y = (cx - cy) * 24 * scale, (cx + cy) * 12 * scale
        left, top, right, bottom = 0, 0, 48, 24
        if record["flags"] & 1:
            left, top = min(left, record["ex"]), min(top, record["ey"])
            right, bottom = max(right, record["ex"] + record["ew"]), max(bottom, record["ey"] + record["eh"])
        if frame in frames:
            f = frames[frame]
            hdpack.require(f["x"] >= left * scale and f["y"] >= top * scale and f["x"] + f["width"] <= right * scale and f["y"] + f["height"] <= bottom * scale, "HD terrain frame exceeds classic union envelope")
        else:
            hdpack.require(lookup is not None, "A palette is required to preview classic terrain fallback")
            fallbacks.append(frame)
        work += (right - left) * (bottom - top) * scale * scale
        placements.append((x, y, frame, left * scale, top * scale, right * scale, bottom * scale))
    hdpack.require(work <= 32 * 1024 * 1024, "Terrain preview sample budget exceeded")
    left = min(p[0] + p[3] for p in placements)
    top = min(p[1] + p[4] for p in placements)
    right = max(p[0] + p[5] for p in placements)
    bottom = max(p[1] + p[6] for p in placements)
    width, height = right - left, bottom - top
    hdpack.require(0 < width <= 4096 and 0 < height <= 4096, "Terrain preview canvas exceeds 4096 pixels")
    rgba = bytearray(bytes((48, 48, 48, 255)) * (width * height))
    coverage = bytearray(width * height)
    depths = array("i", [0x7fffffff]) * (width * height)
    for x, y, number, fl, ft, fr, fb in sorted(placements, key=lambda p: (p[1], p[0], p[2])):
        record, f = records[number], frames.get(number)
        for py in range(ft, fb):
            for px in range(fl, fr):
                sample = classic_tile_sample(classic, record, px // scale, py // scale)
                if sample is None:
                    continue
                index, depth, classic_visible = sample
                alpha, shadow = 255, 0
                sx, sy = (px - f["x"], py - f["y"]) if f else (-1, -1)
                if f and 0 <= sx < f["width"] and 0 <= sy < f["height"]:
                    offset = sy * f["width"] + sx
                    r, g, b, alpha = f["color"][offset * 4:offset * 4 + 4]
                    depth = -struct.unpack_from("<h", f["depth"], offset * 2)[0]
                    shadow = f["shadow"][offset] if f["shadow"] else 0
                    if lookup and f["remap"] and f["remap"][offset]:
                        r, g, b = lookup[15 + f["remap"][offset]]
                else:
                    if not classic_visible:
                        continue
                    hdpack.require(lookup is not None, "A palette is required for pixels outside an HD terrain crop")
                    r, g, b = lookup[index]
                value = -y // scale - 24 + depth
                target = (y + py - top) * width + x + px - left
                if not alpha or value > depths[target]:
                    continue
                for channel, source in enumerate((r, g, b)):
                    before = rgba[target * 4 + channel]
                    source = (source * (255 - shadow) + (before // 2) * shadow + 127) // 255
                    rgba[target * 4 + channel] = (source * alpha + before * (255 - alpha) + 127) // 255
                coverage[target] = 255
                depths[target] = value
    return width, height, bytes(rgba), bytes(coverage), {"scale": scale, "origin": [left, top], "placements": layout["placements"], "classic_fallback_frames": sorted(set(fallbacks))}


def terrain_preview(pack, classic, directory, layout=None, palette=None):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    width, height, rgba, coverage, metadata = assemble_terrain(pack, classic, layout, palette)
    (directory / "terrain-seams.png").write_bytes(hdpack.png_write(width, height, rgba))
    (directory / "terrain-coverage.png").write_bytes(hdpack.png_write(width, height, coverage, 1))
    (directory / "terrain-layout.json").write_text(json.dumps(metadata, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    page = '<!doctype html><meta charset="utf-8"><title>Terrain seam preview</title><style>body{font:16px system-ui;background:#222;color:white}img{image-rendering:pixelated;max-width:100%}</style>'
    page += f'<h1>{html.escape(pack["name"])} terrain seams</h1><p>Neutral source-color, logical-depth assembly at scale {metadata["scale"]}. Gray is uncovered background. Coverage shows holes and seam boundaries. Palette and engine lighting/fog are separate; this authoring image is not runtime acceptance.</p><img src="terrain-seams.png"><h2>Coverage</h2><img src="terrain-coverage.png">'
    (directory / "index.html").write_text(page, encoding="utf-8", newline="\n")
