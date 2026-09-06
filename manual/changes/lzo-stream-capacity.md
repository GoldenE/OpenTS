---
title: Size LZO work buffers for native pointers and complete blocks
category: fix
release: 0.1.0
targets:
- type: format
  id: save-games
  effect: changed
---

Saving a game on x64 no longer indexes beyond the LZO compressor's work memory. The save stream and packed-data wrappers size that memory for native pointers and reserve room for blocks that grow during compression.

Partial save-stream blocks now record their actual decoded length. The reader continues to accept older Win32 blocks whose final header declared a full block despite containing a shorter tail. The eight-byte block header and the compressed data format remain unchanged.
