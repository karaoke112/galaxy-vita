# Miis

On the Wii, Super Mario Galaxy lets each save file use one of the console's Miis as its icon. The port
builds the Wii's Mii library (RVLFaceLib, from the decompilation) and reads the player's Miis from the
memory card:

| Path | What it is |
| --- | --- |
| `ux0:data/mii/wii/RFL_DB.dat` | the player's Miis as the Wii's Mii database (the Wii's `/shared2/menu/FaceLib/RFL_DB.dat`) |

[Mii Maker Vita](https://github.com/karaoke112/mii-maker-vita) writes that file every time its Mii list
is saved (the first 100 Miis; parts the Wii does not have become the Wii's default ones). A copy of
`RFL_DB.dat` from a Wii's NAND (or Dolphin's) works too. Without the file the game behaves as a Wii whose
Mii Channel has no Miis, as before. The face pictures come from the disc's own face resource
(`ObjectData/MiiFaceDatabase.arc`), so nothing else is needed.

The port only reads the Mii folder: the game's NAND calls that would create, write or delete files there are
refused.

## How it works

- `platform/src/nand/nand.cpp` maps the NAND folder `/shared2/menu/FaceLib` to `ux0:data/mii/wii`, refuses
  writes there, and has the asynchronous NAND calls RFL uses (their callbacks come from an alarm after the
  call returns, as on the Wii).
- `platform/src/sdk/rfl_port.cpp` turns RFL's big-endian files into this host's layout: the database as it
  is read (bit-fields rebuilt in little-endian order, text and ID words swapped, the CRC checked on the file
  and made again; an invalid file reads as an empty database rather than one RFL would call broken), and the
  face resource once, when `RFLInitResAsync` receives it (archive tables, texture headers, the shapes'
  counts, vectors and vertex arrays; texels and index streams stay as they are).
- The library's C files build with two small helpers in `platform/include/port/rfl_c/` (C versions of the
  C++ header names they include, and CodeWarrior's `LENGTHOF`).
- `tools/test_rfl_port.sh` checks both conversions against RFL's own structures on a little-endian host.
