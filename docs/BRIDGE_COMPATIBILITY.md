# Addon and desktop compatibility

The desktop app checks the addon version, its required minimum desktop version and the supported protocol range. Matching release numbers are not required when the declared ranges remain compatible.

## Automatic check

Install the complete addon folder and desktop package. Keep the game visible and run `/bl version`, or click **Check compatibility** in the addon settings. The desktop **Status** page shows the detected versions and the result.

- **Compatible**: pixel chat can be processed normally.
- **Update desktop / update addon**: pixel chat processing is paused. Pending incoming pixel-chat jobs are discarded; already dispatched translation requests can finish. Manual output and explicitly selected OCR remain separate paths.
- **Not confirmed**: no usable version announcement has arrived. Older readable chat formats can continue, but the app does not label them compatible. Run `/bl version`; update the addon if the command is unavailable.
- **Invalid or unsupported announcement**: pixel chat is paused and an update warning is displayed.

Windows issues a tray warning when a mismatch or unconfirmed legacy connection is first detected. The result remains visible in **Status**, even if another operation changes the short status note.

Announcements are sent at login/enabling, explicit checks, layout synchronization and replay. Accepted chat traffic can attach an announcement at most once per 15 seconds so an app opened later can discover the version. There is no idle heartbeat, timed chat replay or added idle pixel redraw. Compatibility messages are not translated, sent to an AI service or recorded as chat bodies.

## Manual reverse confirmation

The pixel bridge is one-way. The addon cannot automatically inspect the desktop process, executable version or clipboard.

1. Receive a compatible announcement in the desktop app.
2. Open **Settings → Status → Copy compatibility code**.
3. Paste the complete copied `/bl pair ...` command into the game chat input and press Enter.
4. The addon checks the desktop version and supported protocol range, then sends the result back through the existing pixel bridge.

The command is bound to the current addon session and a fresh desktop nonce. Another game session's command is rejected. Only a confirmation for the current desktop nonce is reported as confirmed. Confirmation is a version receipt, not a continuous presence or authentication check. It is kept in addon memory and cleared by `/reload` or restarting the game; it is optional for normal translation.

If a valid reverse confirmation identifies a mismatch, the addon pauses new chat and replay transmission while still allowing compatibility announcements. **Reset manual confirmation** clears this state so the versions can be checked again.

After entering the world with a new addon version, BianLingo shows the minimum Windows app version and a copyable GitHub Releases link. Click **Close** to dismiss it for that version. **Don't show update reminders again** disables future notices, and **Show update reminders** in the addon settings turns them back on. WoW saves these choices on `/reload` or normal exit. The minimum desktop version remains available in settings and through `/bl version`.

## Maintainer configuration

`resources/bridge-compatibility.json` defines the supported protocol range and minimum component versions. Run `python tools/generate_bridge_compatibility.py` after changing the project version or this policy. It generates `addon/BianLingo/Compatibility.lua`; all client TOCs load this module before the bridge. Packaging rejects stale generated constants.

The stable BLG2 `BRIDGE_READY` event (18) carries a bounded ASCII `BLV1` announcement: addon version, minimum desktop version, protocol minimum/maximum, optional confirmed desktop version, nonce and game-side mismatch flag, separated by tabs. The addon author field is empty. Future incompatible payload protocols must retain this discovery and announcement envelope so older desktop apps can identify the mismatch.

The reverse `BLP1` command carries the desktop version, minimum addon version, protocol range, target addon session and desktop nonce. Parsing accepts bounded numeric versions and fields only; pasted text is never executed as Lua. This code contains no API keys, account credentials or chat content.
