---
format_id: scenario-terrain
title: Scenario terrain data
summary: Stores a scenario's terrain cells in the Base64-encoded IsoMapPack sections.
kind: file
filenames:
  - "*.MAP"
  - "*.MPR"
source_files:
  - code/display.cpp
  - code/map.cpp
  - code/ini.cpp
  - code/lzopipe.cpp
  - code/lzostraw.cpp
  - code/session.cpp
  - code/preview.cpp
  - code/netshare.cpp
related:
  - type: format
    id: ini-syntax
---

Scenario map files are INI files, but their terrain cells are stored as binary data. The binary stream is Base64-encoded and split across the numbered assignments of an `[IsoMapPack]` section. The later revisions use `[IsoMapPack2]` through `[IsoMapPack5]` instead.

The loader checks all five sections in revision order. A missing section has no effect. If more than one revision is present, each one that decodes is applied, so a later section may replace terrain loaded by an earlier one. The current map writer clears all five sections and writes only `[IsoMapPack5]`.

## Revisions

| Section | Compression | Cell data |
| --- | --- | --- |
| `[IsoMapPack]` | LCW | Tile type, sub-tile and height for the original 128 by 128 cell grid |
| `[IsoMapPack2]` | LCW | Sparse cell records terminated by `CELL_NONE` |
| `[IsoMapPack3]` | None | Sparse cell records terminated by `CELL_NONE` |
| `[IsoMapPack4]` | LZO | Sparse cell records terminated by `CELL_NONE` |
| `[IsoMapPack5]` | LZO | Version 4 fields plus the cell's ice-growth flag |

An IsoMapPack5 cell record carries the cell coordinate, isometric tile type, sub-tile, height and ice-growth flag. LZO divides that record stream into blocks of at most 8 KiB; each block begins with the compressed and uncompressed byte counts. The final `CELL_NONE` coordinate is not followed by the rest of a cell record.

IsoMapPack5 accepts at most the LZO block storage required for one record per map cell and the terminating `CELL_NONE`. A section that reaches beyond that bound is reported and not applied; the initial fill or terrain from an earlier pack revision remains. The readers for revisions 1 through 4 each accept up to 512,000 Base64-decoded bytes.

## Optional map-selection preview

`[Preview] Size=` supplies the selection image's pixel width and height as the last two numbers of a four-number rectangle. `[PreviewPack]` carries Base64-encoded LZO data containing three bytes per pixel in red, green, blue order. The preview reader scans the file only up to `[Map]`, so place both preview sections before that section.

The image is optional. A missing preview pack or nonpositive image dimensions leaves no preview to display, and the map remains selectable. Preview drawing also skips an empty image or an empty destination frame. These checks affect the selection picture, not the map's terrain dimensions or simulation.
