# OCR input and fallback

OCR has two modes. **Fallback when chat is restricted** starts when the addon reports hidden chat text. **OCR-only mode** continuously recognizes visible game chat, ignores addon chat packets and needs no visible pixel strip. Both use the active translation connection, including compatible local engines. OCR is currently implemented for Windows; macOS support is under development.

## First setup

1. Install matching desktop and standalone `BianLingo` addon versions, then `/reload`. The addon's 4×4 marker reports normal/restricted chat; automatic fallback needs that status. OCR-only mode does not require the addon strip, but does require a supported game window and legible native chat.
2. Open **Settings → Translation scope → OCR settings**.
3. Open the game, select **Choose chat region** and drag around native chat text, excluding titles, input and unrelated UI. Esc cancels.
4. English is the default recognition language. Russian, Korean and Japanese are also available; select at most three simultaneously. More languages can increase recognition cost and errors.
5. Choose OCR-only or automatic fallback, apply the draft and **Save settings** in the main window. First enabling OCR adds its message type to the first tab; other tabs can select it separately.
6. Install recognition data, then **Test recognition**. The preview recognizes locally without requesting a translation.

Regions are stored relative to the game client area and follow movement. Resize/DPI changes pause recognition until you choose/save a new region. Status and recognition timing are shown in OCR settings and Status.

## Behavior and limits

- Capture targets the game window, not the desktop or translation overlay. The OS may display a capture border.
- Recognition accepts chat-prefixed text, joins adjacent wrapped lines and skips low-confidence results. Modified chat formats are not all supported.
- The region is checked at most every 500 ms, recognition runs no more often than once per second and requires two stable observations. Delay is commonly several seconds; fast scrolling can lose messages. One OCR worker runs without a screenshot backlog.
- Initial takeover reads at most the bottom three visible messages. Fallback compares recent pixel bodies for 60 seconds. Disjoint scroll/tab history resets the baseline to avoid flooding, but text matching is less reliable than protocol identities.
- OCR messages are labelled `[OCR]`, sorted by first observation and read-only without reply/whisper actions. Names are not verified identities, so target-script nickname filtering is not applied. Ordinary body/noise filters still apply.
- Tabs can select the OCR message type. These records do not impersonate game channels or interactive item links. Importing a game layout may replace tab filters; re-enable OCR where needed.
- If logging is enabled, recognized text, OCR source and processing results are recorded, not screenshots. Images are not sent to the model; recognized text follows the selected local/remote translation path.
- Recognition pauses when the game is minimized/closed, capture is unsupported or the region is invalid. HDR uses floating-point window capture converted to grayscale for OCR; display-mode changes recreate the session. Game settings, UI scaling and chat colors still need HDR field evaluation.
- Only visible text can be recognized. OCR cannot recover never-observed secret data or guarantee complete history.

## Models and dependencies

The Windows implementation uses native Tesseract 5.5.3 and needs no Python. Recognition data is installed explicitly into `UserData/OCR` next to the EXE and retained between runs. Do not distribute personal settings or screenshots with it.

Data is pinned to official `tessdata_fast` 4.1.0 commit `65727574dfcd264acbb0c3e07860e4e9e9b22185`, with SHA-256 embedded in the app. Public downloads may try a configured GitHub proxy before the publisher URL; both paths require verification and carry no API key. Corrupt files are not loaded. Matching verified `.traineddata` files can be copied into the directory for offline preparation.

Component notices include Tesseract, Leptonica, language data and static dependencies. The Mac adapter has no OCR implementation yet.

## Validation limits

Automated checks cover Lua restriction markers/recovery, idle-state painting, protocol compatibility, OCR settings, wrapped lines, sequence deduplication and read-only interaction. An SDR host and forced FP16 capture probe do not establish real HDR gameplay, restricted boss-fight behavior or frame-rate cost. Those remain field-validation items.
