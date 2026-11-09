# Chat tabs and replies

The overlay supports 1–16 named tabs, with names of 1–24 characters. A single default tab hides the tab bar for a compact appearance.

## Create tabs

Open the cat settings button, select **Chat tabs**, add a tab, name it, select message types and save. Party/leader, raid/leader and incoming/outgoing whispers can be selected separately. Selecting no types creates an empty tab. Right-click an overlay tab to open management. When tabs exceed the width, use the arrows or scroll over the tab bar.

Public channels can be included/excluded by number or full name, separated by commas or semicolons. An empty include list includes all channels; exclusions take priority. For example, include `Trade` and exclude `LookingForGroup`. Name matching ignores ASCII case and a ` - zone` suffix, avoiding dependence on channel numbers after a zone change. This is not a message-body keyword filter.

**Translation scope** is the global first filter: a tab cannot enable a globally disabled channel. A message needed by any tab is translated once and stored in shared history; switching tabs filters existing results. Messages matched by no tab are not sent for translation, while received originals still follow the journal's normal input path. The history limit applies to each tab separately. Busy public channels do not evict the latest retained party, guild or whisper messages. Overlapping tabs share the same stored result without additional translation requests. At most 16 times the per-tab history limit is retained.

Changing filters only reveals retained results; it does not re-request previously skipped or evicted messages. Each tab retains its reading position during the current run. Inactive tabs count new messages and clear unread state when selected. Names, conditions and the active tab persist; message history, unread counts and reading positions do not persist across application restarts.

## Synchronize the game layout

1. Create/configure the game's chat windows, names, message types and public channels.
2. Keep the desktop app running with game pixel data visible. Run `/bl windows`, or use **Synchronize chat windows** in `/bl settings`.
3. Once the app receives and validates the complete snapshot, it replaces all overlay tabs, saves and applies the layout immediately. No second import/save action is needed.

Synchronization preserves global translation switches. Disabled types are reported after synchronization and in the selected tab’s settings; enable them explicitly under **Translation scope** if desired. Community streams retain their native `Community:clubId:streamId` condition instead of becoming public channels. Update both the addon and desktop app, then synchronize again to apply stream-specific routing. No extra player lookup is needed.

Synchronization replaces the whole layout; it does not merge default tabs, custom tabs or a previous snapshot. Two exported game windows produce two overlay tabs with the exported names, order, supported message types and public-channel conditions. Synchronize again after renaming/removing game windows. The active tab is retained if its ID remains, otherwise the first is selected.

Invalid/incomplete snapshots or failed settings writes leave the existing layout intact and show an error. Identical layouts cause no redundant settings writes or chat relayout. Only `chat_tabs` and the necessary `active_chat_tab` change: unsaved API, prompt, shortcut and appearance drafts are not committed, translation caches are not cleared and outgoing work is not cancelled. The tab editor draft is replaced by the imported layout. Export is always user-triggered, without periodic queries or replay.

Imported fields are names, supported chat events and registered public-channel names. Combat logs, window coordinates/sizes, third-party filters and player-specific whisper windows are outside the import scope. Windows with no transferable message types are skipped. Layout import does not transmit chat already rejected by addon language/noise filters.

Layout uses BLG2 control event `BRIDGE_CHAT_LAYOUT` (22), with a maximum 12 KiB snapshot and strict field, structure, ID and count validation. It contains no chat bodies or credentials, yields to live chat and does not enter chat history/translation/journaling. Incomplete snapshots require another manual export. The last successful snapshot is cached as `game-chat-layout.json` in application data for explicit recovery; a separate cache-write error does not undo a successful main-settings save.

The game export uses native message/channel registration exposed in [Blizzard ChatFrame.lua](https://github.com/Gethe/wow-ui-source/blob/classic_era/Interface/AddOns/Blizzard_ChatFrameBase/Shared/ChatFrame.lua). Restricted/secret values or missing APIs report failure rather than bypassing restrictions.

## Pointer interaction and preview

Real replyable messages expose a return-arrow cursor over the entire visible row, including trailing whitespace. Left-click replies in the source channel; a received whisper targets its sender. Right-click opens the **Whisper / View level / Block player** menu. Input focus moves to the reply box. Full sender/realm identities are retained internally and in logs. A whisper command omits the realm suffix only when the addon confirms the same realm; cross-realm, conflicting or unknown identity keeps the full name. Displayed names remain compact.

The input layer covers the fixed message viewport and scrollbar whitespace; it does not move with message rows or leave holes at the bottom. Empty space does not initiate a reply. Click-through mode yields pointer input to the game. Wheel handling is shared across text, transparent whitespace and the custom scrollbar, supports system line/page settings and fractional wheel steps, and updates reply hit targets after scrolling. Input regions change only on resize; no global mouse hook or periodic redraw is added.

**Show test messages** previews appearance using fictional examples. They intentionally have no reply action/cursor, but support scrolling and tab filters. Exit preview and use a real received message to test replies. See [Message actions and blocking](MESSAGE_ACTIONS_AND_BLOCKLIST.md).

## Same-realm transport

At `PLAYER_LOGIN`, the addon caches `GetNormalizedRealmName`, falling back to `GetRealmName`. It normalizes spaces, hyphens and ASCII case before comparing the sender suffix and realm data already available from the message. Differences/conflicts do not set the same-realm flag. Empty realm fields, short names or a particular Classic version do not count as confirmation. This adds zero player queries; replay retains the cached flag.

The BLG2 player byte keeps class in its low five bits and uses `0x20` for confirmed same realm. `0x40` and `0x80` retain their color/profile roles. Packet size does not grow; the existing checksum covers the flag, which is valid only on chat events. Original author and realm fields are not rewritten.

## Community transport

BLG2 event 24 carries a community message with the same channel header as public chat (`number\tCommunity:clubId:streamId\0`), followed by the existing player metadata, author and body. The receiver normalizes it to `CHAT_MSG_COMMUNITIES_CHANNEL`. Event 17 without a channel header remains readable for unscoped community tabs. IDs 1–23 retain their meanings. Color, identity, replay and checksum handling are shared with ordinary chat packets. The identifier comes from the chat event itself; no community or player lookup is added.
