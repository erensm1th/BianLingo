# Message actions and shared blocklist

The desktop overlay and addon provide per-message actions and a manually synchronized filter list.

## Use

- **Left-click a message row** to reply in its source channel; received whispers reply to their sender. Enter translates/copies the text and restores game focus; you paste/send it yourself.
- **Right-click a message row** for Whisper, View level (in-game `/who`) or Block player. The menu retains a copy of the message, so incoming messages, sorting and asynchronous translation cannot change its target.
- **View level** copies `/who <player>` and attempts to restore the captured game window. Open game chat, paste and submit the command to see its result in game. It does not return a level to the app or add queries to ordinary messages. Cross-realm lookup is not guaranteed. This action is disabled while output translation is busy.
- Manage names under **Settings → Translation scope → Blocklist and addon sync**. Choose **Copy sync code** to transfer the list. Only that explicit action writes the clipboard; blocking a message does not overwrite it.
- Run `/bl blocks` in game, select **Import from desktop**, paste, then **Import and apply**. The list immediately affects later capture. WoW saves `BianLingoDB` through SavedVariables on `/reload` or normal exit and restores it next session.
- Adding/unblocking in the addon sends its list to the desktop. You can also use **Sync desktop** or `/bl blocksync`. No game-path lookup or file write is needed; install the addon manually.

This is the translator's filter list, not Blizzard's account ignore list, and it does not hide native game chat. Battle.net/community identities and OCR messages without a reliable character identity are not treated as confirmed player identities.

## Storage and synchronization

The desktop writes only its own `UserData/blocklist.json`. It does not open the game process, query its executable path, or read/write addon Lua or WTF files. The addon declares `BianLingoDB` in its TOC and stores records in `blocklistState`; WoW performs persistence. Force-closing the game can lose unsaved changes. Distribute the complete clean addon directory without personal list files.

Desktop-to-addon transfer requires copying, pasting and applying a data code. The app does not simulate input, automatically send `/run`/other commands, or read the game's clipboard. Addon-to-desktop uses pixel control event 23 in chunks of at most 32 records with existing live-chat priority; this is not a bidirectional network connection or periodic heartbeat. List synchronization cannot reveal secret chat bodies.

Blocking a received message preserves full player-realm identity. A manually entered name without a realm matches all characters with that name. Records merge by logical version; unblocking retains a record so an older code cannot reapply a newer removal. An empty snapshot does not clear the other side. Capacity is 512 records including removals. Concurrent edits are ordered by logical version and origin, not guaranteed real-world clock order across devices.

## Data codes

BLGB1 is data, not Lua code. The header is `BLGB1|<8-digit-hex-adler32>|<record-count>`. Each following line contains a hex-encoded UTF-8 name, decimal logical version, ASCII origin ID and 0/1 state, separated by tabs and LF. Pasted CRLF can normalize to LF. The checksum covers the full body. Limits are 256 KiB, 512 records, 192 bytes per name and 64 bytes per origin; versions must be safely representable as Lua integers.

Lua validates the complete checksum, count, fields, UTF-8, duplicates and capacity before merging. Corrupt, truncated or invalid data is rejected atomically. Import uses no `loadstring`, `RunScript` or file I/O. The checksum detects corruption, not identity/authenticity. Codes contain player names; do not share them publicly or import untrusted data.

## Validation limits

Regression tests cover C++ export → real Lua 5.1 validation/import → actual BLG2 chunks → C++ merge, new-session SavedVariables restore, old-code removal behavior, capacity, atomic rejection, paste callbacks, pre-query blocking and live priority during synchronization. UI checks cover clipboard-based transfer and text/padding across supported interface locales.

These isolated checks do not replace real-game clipboard and SavedVariables acceptance. The Windows package includes a standalone addon. macOS support is in development. Reduced process/file contact is not evidence of Blizzard approval or a guarantee of account safety.
