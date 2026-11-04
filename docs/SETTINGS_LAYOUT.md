# Settings layout and target-script nickname filtering

## Responsive layout

`src/ui/windows_settings_layout.inc` calculates rows, columns, group heights and scrolling from visible controls rather than maintaining separate fixed background rectangles.

- Start a group with `settings_section(page)`. Subsequent `page_control`, `label`, `checkbox` and `edit` calls belong to that section. Seed coordinates express row/column relationships, not group bounds.
- Groups have 16 logical pixels of padding and 12-pixel row/group gaps. Controls in a row retain separation.
- Layout measures translated text with its actual font. Descriptions, toggles and buttons can wrap. Columns can give width to long words, or stack when a row no longer fits. Edits and choices also receive enough font height.
- Client width and DPI determine bounds. Navigation, headings, descriptions and Classic group labels adjust together. Keyboard focus scrolls an offscreen control into view.
- Text, visibility, font and width changes trigger layout. Measurement is cached; there is no periodic layout/redraw task or complete form remeasurement on each typed character.

Add new options to a section. Do not restore independent background coordinates. Check all selectable interface languages after changing captions/fonts, rather than validating one language alone. **Chat appearance → Interface language** offers Follow system and explicit native-name choices; saving refreshes captions/fonts in place without resetting translation targets or custom prompts.

## Regression checks

```powershell
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release --output-on-failure
```

The Windows gate runs nine UI locales, three built-in theme families, two widths and all six pages: 324 page-layout combinations. It checks unique group membership, padding, overlap, text width/height and navigation height. Skin interaction, sliders, pickers, buffer/GDI lifetime and idle painting checks remain alongside pixel, OCR, connection and Lua tests.

UI fixtures use isolated `ui-test-state/run-*` settings/skin directories in the build tree, not real API credentials or user settings. A `desktop_ui` resource lock serializes tests. Rendering comparisons scroll controls into view and recreate references after resizing rather than treating offscreen pixels as errors.

Screenshots are retained under `.local/qa/settings/<locale>/`. Host-DPI tests do not substitute for other display scales or mixed-DPI hardware. macOS support is in development and has no release validation yet.

## Nickname filter

**Skip players whose names use the target language's script** is off by default and follows the chat translation target, initially derived from the system language. Missing targets fall back to the system default.

Only author/player names already present in the chat event are inspected, excluding the realm suffix. No player query or language-detection API is added. A match retains the whole original message and skips both online and local translation. Manual output and OCR without reliable identity are unaffected.

Rules cover Han, Kana, Hangul, Cyrillic, Greek, Arabic and Latin scripts. Kana/Hangul names are not treated as Chinese solely because they also contain Han characters. Other-script names do not become Latin matches merely from a few ASCII letters. Script cannot reliably distinguish languages sharing it: Latin names do not prove English/French, and Han names do not distinguish Simplified from Traditional Chinese. This is a user-selected filtering heuristic, not nationality or language identification.
