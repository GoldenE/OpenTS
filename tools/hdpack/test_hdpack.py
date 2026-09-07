# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 OpenTS contributors
# See LICENSE.md for applicable additional terms and warranty disclaimers.
import hashlib
import binascii
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

import hdpack
import hdpreview

VALIDATOR = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 and sys.argv[1] != "-v" else None


class PackTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="opents-hdpack-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        classic = struct.pack("<hhhhhhhhhh3s5sI", 0, 2, 2, 1, 0, 0, 2, 2, 1, 4, b"\x20\x30\x40", bytes(5), 32) + b"\1\2\3\4"
        (self.root / "SYNTH.SHP").write_bytes(classic)
        self.color = bytes([255, 0, 0, 255, 0, 255, 0, 128, 0, 0, 255, 0, 255, 255, 255, 255]) * 4
        (self.root / "color.png").write_bytes(hdpack.png_write(4, 4, self.color))
        (self.root / "mask.png").write_bytes(hdpack.png_write(4, 4, bytes(range(16)), 1))
        self.manifest = {"version": 1, "name": "synth.shp", "classic": "SYNTH.SHP", "variants": [{"scale": 2, "logical_width": 2, "logical_height": 2, "logical_frames": 1, "frames": [{"image": "color.png", "remap": "mask.png"}]}]}
        self.path = self.root / "pack.json"
        self.write_manifest()

    def write_manifest(self):
        self.path.write_text(json.dumps(self.manifest), encoding="utf-8")

    def test_lossless_png_and_wire(self):
        self.assertEqual(hdpack.png_read(self.root / "color.png"), (4, 4, self.color, 4))
        pack, _ = hdpack.load_manifest(self.path)
        wire = hdpack.encode(pack)
        self.assertEqual(wire, hdpack.encode(hdpack.load_manifest(self.path)[0]))
        self.assertEqual(wire[:12], b"ODHP\1\0\0\0\0\0\0\0")
        if VALIDATOR:
            output = self.root / "SYNTH.SHP.HDP"
            output.write_bytes(wire)
            subprocess.run([str(VALIDATOR), "--validate", str(output)], check=True)

    def test_crc_rejected(self):
        path = self.root / "color.png"
        wire = bytearray(path.read_bytes())
        wire[-5] ^= 1
        path.write_bytes(wire)
        with self.assertRaisesRegex(ValueError, "CRC"):
            hdpack.load_manifest(self.path)

    def test_semantic_mask_rejected(self):
        (self.root / "mask.png").write_bytes(hdpack.png_write(4, 4, bytes([17]) * 16, 1))
        with self.assertRaisesRegex(ValueError, "Remap"):
            hdpack.load_manifest(self.path)

    def test_temporal_partial_block_rejected(self):
        self.manifest["variants"][0]["sequences"] = [{"first": 0, "count": 1, "temporal": 2, "reverse": True}]
        self.write_manifest()
        with self.assertRaisesRegex(ValueError, "Incomplete"):
            hdpack.load_manifest(self.path)

    def test_exports_deterministic(self):
        pack, _ = hdpack.load_manifest(self.path)
        first, second = self.root / "first", self.root / "second"
        hdpack.export(pack, first)
        hdpack.export(pack, second)
        self.assertEqual({p.name: p.read_bytes() for p in first.iterdir()}, {p.name: p.read_bytes() for p in second.iterdir()})
        self.assertEqual(hdpack.png_read(first / "s2-f0-t0-a0-d1.png")[2], self.color)

    def test_legacy_preserves_semantics_and_records_loss(self):
        pack, classic = hdpack.load_manifest(self.path)
        palette = bytes([i % 64 for i in range(768)])
        shp, report = hdpack.legacy_shp(pack, classic, palette)
        self.assertEqual(shp[:8], classic[:8])
        self.assertEqual(shp[20:28], classic[20:28])
        self.assertEqual(struct.unpack_from("<I", shp, 28)[0], len(classic))
        self.assertTrue(any("partial-alpha" in line for line in report))
        self.assertEqual((shp, report), hdpack.legacy_shp(pack, classic, palette))

    def test_cli_output_binds_generated_legacy(self):
        palette = self.root / "palette.pal"
        palette.write_bytes(bytes([i % 64 for i in range(768)]))
        output, legacy = self.root / "result.hdp", self.root / "fallback.shp"
        command = [sys.executable, str(Path(hdpack.__file__)), str(self.path), "--output", str(output), "--legacy-shp", str(legacy), "--palette", str(palette), "--accept-lossy-legacy"]
        if VALIDATOR:
            command.extend(["--validator", str(VALIDATOR)])
        subprocess.run(command, check=True)
        wire = output.read_bytes()
        offset = 16 + len("SYNTH.SHP") + 4
        self.assertEqual(wire[offset:offset + 32], hashlib.sha256(legacy.read_bytes()).digest())

    def test_mix_member_offsets_and_name_padding(self):
        wire = hdpack.mix_write({"A": b"first", "AB": b"second", "ABC": b"third", "ABCD": b"fourth"})
        count, size = struct.unpack_from("<hI", wire)
        self.assertEqual((count, size), (4, 22))
        entries = [struct.unpack_from("<iII", wire, 6 + index * 12) for index in range(count)]
        self.assertEqual(entries, sorted(entries))
        expected = {binascii.crc32(name) & 0xffffffff: payload for name, payload in ((b"A\1AA", b"first"), (b"AB\2A", b"second"), (b"ABC\3", b"third"), (b"ABCD", b"fourth"))}
        for crc, offset, length in entries:
            self.assertEqual(wire[54 + offset:54 + offset + length], expected[crc & 0xffffffff])

    def test_paired_shp_metadata_and_envelope(self):
        pack, classic = hdpack.load_manifest(self.path)
        self.assertEqual(hdpack.validate_classic_pair(pack, classic), [])
        variant = pack["variants"][0]
        for key in ("logical_width", "logical_height", "logical_frames"):
            original = variant[key]
            variant[key] += 1
            with self.assertRaisesRegex(ValueError, "logical width/height/frame count"):
                hdpack.validate_classic_pair(pack, classic)
            variant[key] = original
        frame = variant["frames"][0]
        for key, invalid in (("x", -1), ("y", -1), ("width", 5), ("height", 5)):
            original = frame[key]
            frame[key] = invalid
            with self.assertRaisesRegex(ValueError, "exceeds paired SHP crop"):
                hdpack.validate_classic_pair(pack, classic)
            frame[key] = original
        frame.update(x=1, y=1, width=3, height=3)
        self.assertEqual(hdpack.validate_classic_pair(pack, classic), [])

    def test_paired_validation_precedes_output_and_preserves_generated_crop_constraint(self):
        source = self.root / "SYNTH.SHP"
        classic = bytearray(source.read_bytes())
        struct.pack_into("<hhhh", classic, 8, 1, 1, 1, 1)
        source.write_bytes(classic)
        output = self.root / "invalid.hdp"
        command = [sys.executable, str(Path(hdpack.__file__)), str(self.path), "--output", str(output)]
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exceeds paired SHP crop", result.stderr)
        self.assertFalse(output.exists())
        palette = self.root / "palette.pal"
        palette.write_bytes(bytes([i % 64 for i in range(768)]))
        command.extend(["--legacy-shp", str(self.root / "expanded.shp"), "--palette", str(palette), "--accept-lossy-legacy"])
        result = subprocess.run(command, capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exceeds paired SHP crop", result.stderr)
        self.assertFalse(output.exists())

    def test_legacy_nonuniform_crops_preserve_gameplay_area_ranking(self):
        crops = [(1, 1, 1, 1), (0, 1, 3, 2), (-1, 0, 2, 2), (2, 2, 0, 0)]
        classic = bytearray(struct.pack("<hhhh", 0, 4, 4, len(crops)) + bytes(24 * len(crops)))
        frames = []
        for logical, (x, y, w, h) in enumerate(crops):
            radar = bytes([20 + logical, 40 + logical, 60 + logical])
            payload = b"".join(struct.pack("<H", w + 2) + bytes([200]) * w for _ in range(h)) if logical == 1 else bytes([200]) * (w * h)
            record = struct.pack("<hhhhhh3s5sI", x, y, w, h, 0x43 if logical == 1 else 0x41, len(payload), radar, bytes([logical]) * 5, len(classic) if w * h else 0)
            classic[8 + logical * 24:8 + (logical + 1) * 24] = record
            classic.extend(payload)
            if w and h:
                frames.append(dict(logical=logical, subframe=0, facing=0, direction=1, x=x * 2, y=y * 2, width=w * 2, height=h * 2,
                                   color=bytes([220, 100, 30, 255]) * (w * h * 4), remap=bytes(1 + i % 16 for i in range(w * h * 4)), shadow=b"", depth=b""))
        pack, _ = hdpack.load_manifest(self.path)
        variant = pack["variants"][0]
        variant.update(logical_width=4, logical_height=4, logical_frames=4, frames=frames)
        palette = bytes([i % 64 for i in range(768)])
        output, report = hdpack.legacy_shp(pack, bytes(classic), palette)
        _, _, decoded = hdpreview.decode_shp(output)
        self.assertEqual(output[:8], classic[:8])
        before, after = [], []
        for logical, (x, y, w, h) in enumerate(crops):
            offset = 8 + logical * 24
            self.assertEqual(output[offset:offset + 8], classic[offset:offset + 8])
            self.assertEqual(output[offset + 12:offset + 20], classic[offset + 12:offset + 20])
            ow, oh = struct.unpack_from("<hh", output, offset + 4)
            before.append(w * h)
            after.append(ow * oh)
            if w and h:
                expected = bytes(16 + ((row * 2 + 1) * (w * 2) + col * 2 + 1) % 16 for row in range(h) for col in range(w))
                self.assertEqual(decoded[logical][4], expected, "Master sampling must include the original crop origin")
                self.assertEqual(output[offset + 8:offset + 10], classic[offset + 8:offset + 10], "Compression mode and unrelated frame flags remain intact")
            else:
                self.assertEqual(output[offset:offset + 24], classic[offset:offset + 24])
        self.assertEqual(before, after)
        self.assertEqual(max(range(4), key=lambda i: after[i]), 1, "AnimType Biggest frame remains frame1")
        self.assertTrue(any("crop-area ranking" in line for line in report))
        self.assertEqual(hdpack.validate_classic_pair(pack, output), ["scale 2: absent logical frames 3 use paired classic artwork"])

    def test_legacy_rle_preserves_long_transparent_runs(self):
        width = 300
        old_row = struct.pack("<H", 7) + bytes([0, 255, 0, 44, 200])
        classic = struct.pack("<hhhh", 0, width, 1, 1) + struct.pack("<hhhhhh3s5sI", 0, 0, width, 1, 3, len(old_row), bytes(3), bytes(5), 32) + old_row
        color = bytearray(width * 4)
        color[-4:] = bytes([200, 100, 40, 255])
        frame = dict(logical=0, subframe=0, facing=0, direction=1, x=0, y=0, width=width, height=1, color=bytes(color), remap=bytes([0] * 299 + [1]), shadow=b"", depth=b"")
        variant = dict(scale=1, logical_width=width, logical_height=1, logical_frames=1, frames=[frame])
        output, report = hdpack.legacy_shp({"kind": "shape", "variants": [variant]}, classic, bytes([i % 64 for i in range(768)]))
        self.assertEqual(struct.unpack_from("<h", output, 16)[0] & 2, 2)
        self.assertEqual(hdpreview.decode_shp(output)[2][0][4], bytes(299) + bytes([16]))
        self.assertIn("RLE encoding", report[0])

    def test_absent_frame_fallback_diagnostics(self):
        pack, classic = hdpack.load_manifest(self.path)
        record = classic[8:32]
        classic = struct.pack("<hhhh", 0, 2, 2, 4) + record * 4 + classic[32:]
        pack["variants"][0]["logical_frames"] = 4
        self.assertEqual(hdpack.validate_classic_pair(pack, classic), ["scale 2: absent logical frames 1-3 use paired classic artwork"])

    def test_palette_preview_mixed_raw_rle_and_bounds(self):
        header = struct.pack("<hhhh", 0, 2, 2, 2)
        record0 = struct.pack("<hhhhhh3s5sI", 0, 0, 2, 2, 1, 4, bytes(3), bytes(5), 56)
        record1 = struct.pack("<hhhhhh3s5sI", 0, 0, 2, 2, 3, 9, bytes(3), bytes(5), 60)
        rle = b"\5\0\0\1\2\4\0\x10\1"
        classic = header + record0 + record1 + bytes([1, 16, 0, 2]) + rle
        palette = bytes(value % 64 for value in range(256) for _ in range(3))
        first, second = self.root / "pal1", self.root / "pal2"
        hdpreview.palette_preview(classic, palette, first)
        hdpreview.palette_preview(classic, palette, second)
        self.assertEqual({p.name: p.read_bytes() for p in first.iterdir()}, {p.name: p.read_bytes() for p in second.iterdir()})
        pixels = hdpack.png_read(first / "fallback-1.png")[2]
        self.assertEqual(pixels, bytes([0, 0, 0, 0, 8, 8, 8, 255, 64, 64, 64, 255, 4, 4, 4, 255]))
        bad = bytearray(classic)
        bad[63] = 0
        with self.assertRaisesRegex(ValueError, "transparent run"):
            hdpreview.decode_shp(bad)

    def terrain_fixture(self, count=1, extra=False):
        body, offsets = bytearray(), []
        for number in range(count):
            offsets.append(16 + count * 4 + len(body))
            record = bytearray(52)
            struct.pack_into("<9iI", record, 0, 0, 0, 1204 if extra else 0, 628, 1206 if extra else 0, 22 if extra else 0, 11 if extra else 0, 2 if extra else 0, 1 if extra else 0, 3 if extra else 2)
            body.extend(record + bytes([number + 1]) * 576 + bytes(576))
            if extra:
                body.extend(bytes([number + 1]) * 2 + bytes(2))
        classic = struct.pack("<iiii", count, 1, 48, 24) + struct.pack("<" + "I" * count, *offsets) + body
        frame = dict(logical=0, subframe=0, facing=0, direction=1, x=0, y=0, width=96, height=48, color=bytes([200, 100, 40, 255]) * (96 * 48), remap=b"", shadow=b"", depth=bytes(96 * 48 * 2))
        variant = dict(scale=2, logical_width=48, logical_height=24, logical_frames=count, facings=1, frames=[frame], sequences=[], voxel=b"", motion=b"", model=[0] * 9)
        return {"name": "SYNTH.TEM", "kind": "terrain", "digest": hashlib.sha256(classic).digest(), "variants": [variant]}, bytes(classic)

    def test_assembled_terrain_seams_are_contiguous_and_deterministic(self):
        pack, classic = self.terrain_fixture()
        self.assertEqual(hdpack.validate_classic_pair(pack, classic), [])
        width, height, rgba, coverage, metadata = hdpreview.assemble_terrain(pack, classic)
        self.assertEqual((width, height, coverage.count(255)), (192, 96, 4 * 576 * 4))
        for y in range(height):
            visible = [x for x in range(width) if coverage[y * width + x]]
            if visible:
                self.assertEqual(len(visible), visible[-1] - visible[0] + 1, "Internal terrain seam has a hole")
        self.assertEqual(metadata["classic_fallback_frames"], [])
        first, second = self.root / "terrain1", self.root / "terrain2"
        hdpreview.terrain_preview(pack, classic, first)
        hdpreview.terrain_preview(pack, classic, second)
        self.assertEqual({p.name: p.read_bytes() for p in first.iterdir()}, {p.name: p.read_bytes() for p in second.iterdir()})
        self.assertEqual(hdpack.png_read(first / "terrain-seams.png")[2], rgba)

    def test_terrain_fallback_palette_overlap_and_layout_limits(self):
        pack, classic = self.terrain_fixture(2)
        palette = bytes(value % 64 for value in range(256) for _ in range(3))
        layout = {"placements": [{"frame": 1, "cell_x": 0, "cell_y": 0}]}
        with self.assertRaisesRegex(ValueError, "palette is required"):
            hdpreview.assemble_terrain(pack, classic, layout)
        _, _, rgba, coverage, metadata = hdpreview.assemble_terrain(pack, classic, layout, palette)
        pixel = coverage.index(255)
        self.assertEqual(rgba[pixel * 4:pixel * 4 + 4], bytes([8, 8, 8, 255]))
        self.assertEqual(metadata["classic_fallback_frames"], [1])
        self.assertEqual(hdpack.validate_classic_pair(pack, classic), ["scale 2: absent terrain frames 1 use paired classic artwork"])
        pack["variants"][0]["logical_width"] = 47
        with self.assertRaisesRegex(ValueError, "logical terrain metadata"):
            hdpack.validate_classic_pair(pack, classic)
        pack["variants"][0]["logical_width"] = 48
        pack["variants"][0]["frames"][0]["x"] = -1
        with self.assertRaisesRegex(ValueError, "TMP footprint"):
            hdpack.validate_classic_pair(pack, classic)
        pack["variants"][0]["frames"][0]["x"] = 0
        pack, classic = self.terrain_fixture(2, extra=True)
        classic = bytearray(classic)
        record = struct.unpack_from("<I", classic, 20)[0]
        classic[record + 52] = 0
        _, _, rgba, coverage, _ = hdpreview.assemble_terrain(pack, classic, layout, palette)
        self.assertEqual((rgba[44 * 4:44 * 4 + 4], coverage[44]), (bytes([0, 0, 0, 255]), 255), "Base palette index zero remains visible with extra imagery")
        pack, classic = self.terrain_fixture(extra=True)
        pack["variants"][0]["frames"][0]["color"] = bytes([200, 100, 40, 128]) * (96 * 48)
        layout["placements"][0]["frame"] = 0
        width, _, rgba, _, _ = hdpreview.assemble_terrain(pack, classic, layout)
        offset = (22 * width + 44) * 4
        self.assertEqual(rgba[offset:offset + 4], bytes([124, 74, 44, 255]), "Overlapping extra imagery must compose once")
        layout["placements"][0]["cell_x"] = 999
        with self.assertRaisesRegex(ValueError, "cell x"):
            hdpreview.assemble_terrain(pack, classic, layout)

    def test_voxel_pair_topology_and_reserved_consumer_diagnostics(self):
        def voxel(mapping):
            layers = len(mapping)
            header = b"Voxel Animation\0" + struct.pack("<4I", 1, layers, layers, 13) + bytes(770)
            headers = b"".join(bytes(16) + struct.pack("<IIB3x", index, 0, 0) for index in mapping)
            body = struct.pack("<II", 0, 4) + bytes([0, 1, 5, 0, 1])
            info = struct.pack("<IIIf12f6f4B", 0, 4, 8, 1, *([0] * 12), 0, 0, 0, 1, 1, 1, 1, 1, 1, 1)
            return header + headers + body + info * layers
        classic = voxel([0, 1])
        pack = {"kind": "voxel", "name": "TEST.VXL", "variants": [{"scale": 2, "voxel": classic}]}
        self.assertEqual(hdpack.validate_classic_pair(pack, classic), [])
        pack["variants"][0]["voxel"] = voxel([1, 0])
        with self.assertRaisesRegex(ValueError, "layer topology"):
            hdpack.validate_classic_pair(pack, classic)
        pack["variants"][0]["voxel"] = voxel([0])
        with self.assertRaisesRegex(ValueError, "layer topology"):
            hdpack.validate_classic_pair(pack, classic)
        self.assertIn("no HD replacement consumer", hdpack.validate_classic_pair({"name": "FONT.FNT", "kind": "font"}, b"")[0])


if __name__ == "__main__":
    unittest.main()
