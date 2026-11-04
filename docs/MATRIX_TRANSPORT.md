# MCT1 compact matrix transport

The addon sends pixels from Lua; desktop adapters share the C++ decoder. Select **Black & white (HDR compatible)** or **Compact matrix** in addon settings, or use `/bl mode compact`. Black & white mode remains the default, and logical message packets use BLG2.

## Display and location

The compact matrix has 40 columns and ten rows. Compact cells default to one physical pixel (42×12 including the border), with two-, three- and four-pixel fallback sizes; with a one-cell black border, total dimensions are 84×24, 126×36 or 168×48. It supports corner placement, window movement, manual replay and idle hiding (enabled by default; explicit saved choices are retained). Idle capture reads only the 4×4 status marker, without extra heartbeats or periodic resends.

Coordinates are zero-based:

| Position | Contents |
|---|---|
| Columns 0–1, all rows | Black/white locator pairs: `11,10,10,11,01,00,11,00,10,01` |
| Column 39, rows 1–8 | Eight gray references, levels 0–7, for each capture color channel |
| Columns 2–37 | 360 data cells, traversed down even columns and up odd columns |
| Column 38 and ends of column 39 | Black; no data |

Each channel carries three bits with luminance `level / 7`. Wider spacing than 16-level encoding does not guarantee resistance to HDR or clipping. Persistent checksum failures should prompt a switch to Black & white mode. HDR behavior still needs hardware acceptance.

## Optical wrapper and bits

The logical frame is a 396-byte BLG2 packet containing original identity/time/channel/player flags, replay batch and checksum. Normal payload capacity is 368 bytes; manual replay leaves 366 bytes per packet. No extra player query is required.

The 405-byte optical wrapper contains ASCII `MCT1` (four bytes), a big-endian Adler-32 of the entire BLG2 frame (four bytes), the 396-byte frame and one zero byte. The outer checksum includes unused logical padding. Signature and trailing zero must match. Checksums detect transport errors; they are not encryption or authentication.

Expand all 405 bytes most-significant-bit first into three consecutive 1080-bit planes assigned to R, G and B. Split each plane into 360 three-bit symbols; data cell i carries symbol i from each plane. Adjacent bytes are not simply packed into one RGB cell.

Decoding validates locator edges, monotonic gray references and minimum contrast, chooses nearest reference values per channel, reconstructs the wrapper, checks its checksum, then lets BLG2 validate lengths, events, flags, batch and checksum. Invalid frames never enter message assembly or translation.

The six-byte 4×4 marker uses version 4 in the high nibble of byte 2. Bit 3 is fixed to one, bit 2 indicates a readable full frame and the low two bits encode cell size minus one. The next 16 bits carry corner (two), chat restriction state (two) and sequence hint (12), followed by CRC-8. Black & white markers accept versions 1 and 2; older MCT1 version-3 markers still decode with size minus two. A marker from a different compact format is not interpreted as MCT1.

## Installation and validation

Install matching app and standalone addon versions, then `/reload`. Preserve your own `UserData` and game SavedVariables; exclude personal data from shared packages.

Location/texture geometry changes only with layout. The decoder reads the three planes without intermediate symbol arrays; textures repaint only when colors change, and duplicate frames are skipped. The wrapper does not change pacing, bounded tail tolerance, caching, live-message priority or deduplication.

Tests execute real Lua 5.1 sending code and decode its pixels in C++. Coverage includes modes, corners, cell sizes, live/replay fields, gamma, clipping and per-cell/channel corruption. Capture checks cover same-poll wakeup, bounded region reads, window movement/resizing and small-window pause. Actual-game HDR and frame-rate effects remain field tests. macOS support is under development; sharing a decoder does not establish a released/tested Mac app.
