# Community skins

Choose a built-in design or a community skin in **Chat appearance**. The bottom of the page provides **Import skin**, **Export current**, **Reload** and **Skin folder**. Importing a UTF-8 JSON file previews it immediately; save settings to retain the selection. Closing settings cancels the preview, but the imported file remains available in the skin folder.

iOS, Terminal, Classic and community skins share the same parser and typed rendering parameters. Widget appearance is selected by those parameters, not by special cases for individual theme names.

## Create and share a skin

Export the current design, edit its ID, name and parameters, then share the JSON file. See the [Obsidian example](../resources/examples/obsidian.theme.json) and [editor schema](../resources/theme.schema.json). Configure the schema in your editor; do not add a `$schema` property to the theme itself. The runtime rejects unknown properties.

```json
{
  "schemaVersion": 1,
  "id": "yourname.midnight",
  "name": "Midnight",
  "author": "Your Name",
  "description": "A dark theme with a cool accent",
  "base": "ios",
  "colors": { "background": "#101418", "accent": "#63D7C1" },
  "widgets": { "toggle": "checkbox", "slider": "square" },
  "metrics": { "sliderPadding": 24, "thumbSize": 16 }
}
```

Required fields are `schemaVersion`, `id`, `name` and `base`. IDs are at most 64 characters, start with a lowercase letter and include a namespace separator (`.`). Remaining characters may be lowercase letters, digits, `-`, `_` or `.`. Consecutive dots, trailing dots and path characters are forbidden. Use your own namespace.

A theme inherits from a built-in base, such as `ios`, `terminal` or `classic`; it cannot inherit from another community theme. Omitted values inherit from the base. Files are limited to 64 KiB and 16 nesting levels. Duplicate keys, unknown fields, invalid types/ranges, symbolic links and unusable directories are rejected. Names and authors are limited to 64 characters, descriptions to 256; control characters are forbidden. Version 1 loads no scripts, DLLs, web pages, images, arbitrary fonts or external assets.

## Installation and storage

On Windows, imported skins are stored in `UserData/Skins/<author.name>/theme.json` next to `BianLingo.exe`. Use **Skin folder** to open the active location. The macOS adapter uses `platform::app_data()/Skins`; macOS support is under development and has no release build yet.

You can install files manually and select **Reload**, or restart the app. The app does not watch the directory or poll files; painting uses cached, typed theme data. Import validates the complete candidate before atomically writing it to the managed directory. A valid file with an existing ID replaces that skin. Up to 64 community skins can coexist.

Missing, invalid or incompatible themes fall back to a built-in appearance with a visible explanation. Theme errors do not reset API connections, channels, prompts or shortcuts. Reload skips broken files and continues loading valid skins. Repair a file and reload to make it selectable again. Application-managed skin installation does not delete existing skins during an update.

## Colors and states

Colors use `#RRGGBB`, without a per-color alpha channel. Chat background transparency is controlled separately by the appearance slider.

| Color fields | Purpose |
|---|---|
| `background`, `surface`, `text`, `accent` | Window, controls, text and accent; also editable in settings |
| `muted`, `disabled` | Secondary text and disabled states |
| `border`, `focus` | Normal and focused borders |
| `hover`, `pressed` | Hover and pressed control states |
| `selection`, `selectedText` | Selected navigation/menu background and text |
| `track`, `thumb`, `toggleOff` | Slider track, thumb/toggle knob and inactive toggle |
| `button`, `onAccent` | Button background and text on an accent background |

Unspecified state colors are derived from the four base colors. Explicit state colors remain explicit when the base colors change. Full export includes all resolved state colors so the result can be shared accurately. Check contrast in normal, focused, disabled and selected states. Game channel, class and item-link RGB values, and pixel-transport colors, are independent of the skin.

## Widgets, dimensions and fonts

| `widgets` field | Values |
|---|---|
| `toggle` | `switch`, `ascii`, `checkbox` |
| `slider` | `round`, `square`, `diamond` |
| `navigation` | `icons`, `plain` |
| `menuMark` | `tick`, `chevron` |
| `frame` | `flat`, `beveled` |
| `invertSelection` | Boolean; uses accent/background as selection colors when not explicitly set |
| `cardBorder` | Boolean; draws a border around settings groups |

Metrics use logical pixels scaled by DPI, except `borderWidth`, which uses physical pixels to keep edges consistent.

| `metrics` field | Range |
|---|---|
| `cardRadius`, `controlRadius`, `buttonRadius` | 0–20, 0–14, 0–14 |
| `borderWidth` | 0–3; zero disables the border |
| `sliderPadding`, `trackThickness`, `thumbSize` | 0–40, 1–8, 8–24; track no thicker than thumb |
| `toggleWidth`, `toggleHeight` | 24–44, 14–22 |
| `popupRowHeight` | 26–44 |

Slider padding is the greater of `sliderPadding` and thumb radius plus one, keeping endpoints inside the control. Painting and pointer input share `SliderGeometry`, so changed dimensions retain matching hit targets.

`fonts` supports `latin: "system-sans" | "system-mono" | "system-serif"`, `cjk: "system"` and `size: 11–14`. Size is the base interface size, adjusted for each control's role; chat text uses its separate setting. Windows chooses system CJK fonts for the selected interface language, including Japanese and Korean. Fonts and buffers are cached rather than allocated during dragging or stable idle periods.

## Classic appearance

Select **Chat appearance → Design style → Classic**. Its initial preset uses the game's default black background at 30% opacity (70% transparency), matching all built-in themes, gold text `#FFD100` and orange tab highlighting `#FF8040`. Message channel, class and item colors remain those carried by the message. Background transparency is remembered per design; returning to a design or cancelling a preview restores its previous value.

Inactive tabs with unread messages fade in for one second and out for one second. Selecting a tab clears its unread state and stops its alert. Classic hides numeric badges while retaining unread counts internally. Test mode demonstrates the glow without changing real counts. A separate 33 ms timer runs only while the overlay/tab bar is visible and an unread tab needs animation; it repaints tabs without relaying out chat or requesting translation. It stops when no alert is needed.

Colors and timing follow the constants and `FCF_StartAlertFlash` in [Blizzard's Classic FloatingChatFrame.lua](https://github.com/Gethe/wow-ui-source/blob/classic_era/Interface/AddOns/Blizzard_ChatFrameBase/Classic/FloatingChatFrame.lua). Outlines, gradients and glows are drawn by the app using system fonts; no game textures or fonts are bundled. This aligns colors and timing, not every texture/font pixel. Alerts follow the overlay's unread state, not the game's per-channel notification settings.

The optional `chat` object is available to any theme:

```json
"chat": {
  "tabs": "classic",
  "flashUnread": true,
  "unreadCount": false,
  "tabText": "#FFD100",
  "tabFill": "#17120D",
  "tabBorder": "#6B5538",
  "glow": "#FF8040",
  "fadeInMs": 1000,
  "fadeOutMs": 1000,
  "defaultBackgroundOpacity": 0.30
}
```

`tabs` accepts `pills` or `classic`. Each fade duration is 100–10000 ms. Default opacity is 0–1 or `null` to retain the current setting; it applies when a design is first selected without saved appearance state. Exports include all resolved chat parameters. The schema and runtime enforce the same ranges. Rendering reads typed `ThemeChat` values; animation does not parse JSON, scan files or allocate drawing buffers.

Classic settings use grouped navigation, flat sections, beveled metallic frames, gold checks on black, red buttons and silver diamond sliders. The chat title row remains transparent with rectangular Classic tabs. A custom variant can inherit these properties:

```json
{"schemaVersion":1,"id":"example.steel","name":"Steel","base":"classic","widgets":{"frame":"beveled","slider":"diamond"},"colors":{"border":"#77746A","button":"#710B0B"}}
```

The Windows GDI painters reuse control buffers and state machines. Hover repaints occur on entering/leaving buttons, without a continuous background animation. Multiline prompt fields retain native EDIT input, selection and IME, with a separate dark scrollbar in the beveled style. Scroll synchronization is event-driven. macOS rendering is under development; exact parity with Windows widget shapes is not promised.

## Preview messages

Select **Show test messages** to display 16 local examples using the real chat renderer. They cover nearby chat, yell, party/leader, raid/warnings, guild/officer, incoming/outgoing whispers, trade, instance chat, the nine Classic classes, item qualities, long multilingual text and replay labels.

Previews honor saved font size, time/original-text display and class/race/gender switches. Theme and palette changes update the preview. Select **Exit test preview** or close settings to restore real history and its reading position. Real messages continue to be received, translated and logged during preview, subject to the normal history limit.

Examples are not received chat: they produce no API requests or chat-log entries, and cannot be replied to. They intentionally have no reply cursor. Scrolling works over transparent whitespace and respects the selected tab. Preview mode is not retained after restart.

## Extension boundaries

Version 1 is a constrained widget-skin system, not a page-layout language or executable extension. New composable properties should extend `ThemeWidgets`/`ThemeMetrics` and platform painters. Changing an existing property's meaning requires a new `schemaVersion`.

Built-ins live in `resources/data/themes.json`; registry and validation are in `src/theme.cpp`. Keep drawing and input separate, linked through typed geometry. Avoid theme-name branches and overlays that cover native control painting. Community skins change local appearance, not capture frequency, translation requests or player queries. Current release validation covers Windows; macOS support is being developed.
