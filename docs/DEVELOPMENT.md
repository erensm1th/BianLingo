# Development

BianLingo consists of a C++20 desktop application, a Lua chat bridge and isolated translation workers. Windows 10/11 x64 is supported; macOS is under development.

## Source layout

- `src`, `include/bianlingo`: application and platform integrations.
- `addon/BianLingo`: game addon, `/bl` commands and `BianLingoDB` settings.
- `resources`: UI text, artwork, themes, model manifests and component notices.
- `tools`, `tests`: build tools, packaging checks and regression tests.

See [Building](BUILDING.md) for build and test commands. CTest covers protocol framing, real Lua transport, settings, providers, UI, blocking and package contents. Optional live-model tests require prepared runtimes and models.

## Defaults and interaction

Fresh profiles use Bergamot, enable all channel groups, retain 20 displayed messages and use 70% background transparency. All built-in themes share that transparency default. Appearance previews remain unsaved until settings are saved. The first launch opens settings, with the overlay aligned to the dialog's lower-left side. Saved positions are retained.

The overlay supports channel replies, a message context menu, chat tabs, manual replay and optional logs. Layout exports replace the tabs automatically. Local models require prepared language pairs; online providers require configured credentials. Prompts apply to LLM connections, including Ollama, while dedicated translation engines use language-pair models.

## Release checks

Run the affected C++/Lua tests and validate the delivered archive before publishing. Package checks exclude personal settings, credentials and logs and bind notices to their corresponding source. Preserve component attribution and source-availability requirements. See [Release compliance](RELEASE_COMPLIANCE.md).

Automated checks do not replace real-game, HDR, mixed-DPI or Windows 10 hardware validation. Restrictions imposed by the game can make chat unavailable. Packages are unsigned unless Authenticode signing is configured. Known limitations and terminology provenance status are recorded in `resources/release-review.json`.

## Component compatibility preview (0.2.11)

The desktop app checks addon minimum versions and protocol ranges from bridge metadata. The addon offers optional, session-bound confirmation through a manually pasted compatibility command; the pixel transport remains one-way. Incompatible pixel chat is paused with an update notice. See [Bridge compatibility](BRIDGE_COMPATIBILITY.md).

The Windows preview was built with `cmake --build build-release --config Release --target BianLingo bianlingo_bridge_compatibility_tests --parallel 8`. The application and compatibility test target compiled, and preview packaging checks passed. Test executables, CTest and application smoke checks were not run. In-game automatic detection and manual confirmation remain pending acceptance. macOS source is retained without validation. The existing formal release review record has not been renewed for this preview.

## Forever message actions

Two-part player names now remain valid whisper and blocklist identities. The Windows preview was built with `cmake --build build-release --config Release --target BianLingo bianlingo_tests bianlingo_blocklist_tests bianlingo_ui_tests --parallel 8`. Targeted CTest checks `core`, `blocklist_core`, `blocklist_lua` and `blocklist_roundtrip` passed, including desktop-to-Lua synchronization of complete first/surname pairs. The real Lua sender also passed early-blocking and malformed-name checks. An isolated execution of the native Forever whisper parser preserved the full recipient separately from the message body.

The UI test executable accepts `--message-actions-preview` to display synthetic Forever and Classic messages without starting capture or translation. This exercises the actual popup menu rather than the smoke-test selection shortcut. Interactive mouse selection and in-game paste/send remain pending acceptance. No full UI suite or macOS checks were run for this fix. The local preview package passed the existing source, addon and runtime packaging checks; it is not a formal release.

## Compatibility and message-action regression review

The Windows Release targets were rebuilt before checking the compatibility and message-action changes. All 28 non-UI CTest checks passed, followed by the English and Chinese Windows UI suites. The final Lua sender and bridge-feature checks also passed after suppressing redundant compatibility announcements for rejected or repeated replay requests. Replay fixtures now wait for a replay payload instead of assuming a fixed frame offset after adding version metadata.

Opening the output composer from a popup menu now retains the captured game as its return destination when the popup has activated a BianLingo window. Other applications remain valid shortcut return targets, and closed or missing game windows retain the previous behavior. UI coverage includes message targeting, input focus, row whitespace, wheel routing, theme changes, layouts and the captured-window return policy. The UI stress report recorded 30 style switches, five theme variants, no new paints or buffer allocations during 200 idle polls, and stable GDI handle counts.

The review used isolated fixtures and did not run paid API requests or new model benchmarks. Other UI locales, real dungeon/restriction transitions, HDR hardware and macOS were not revalidated. Logs are stored locally in `.local/ctest-regression-review.log`, `.local/ctest-ui-review-en.log` and `.local/ctest-final-ui-and-replay-review.log`. The reviewed preview remains local and does not renew the formal release approval record.

## Initial layout synchronization

The capture regression first reproduced a missed initial synchronization during the five-second discovery backoff. Windows now probes the four fixed 4×4 marker locations at 250 ms intervals while searching; established idle capture remains one marker read per 100 ms. Broad corner scans retain their existing throttle. The Lua sender gives explicit layout snapshots three bounded passes with stable identities, repeated version metadata, a one-second initial version frame and two ticks per data frame. These complete snapshots do not depend on the 32-frame chat tolerance tail, and live chat continues to preempt them.

Release builds of `BianLingo`, `bianlingo_capture_tests` and `bianlingo_bridge_tests` completed. Eight related CTest checks passed: capture, Lua sender, bridge features, version compatibility, chat tabs and the three blocklist checks. Added fixtures cover all four corners and supported cell sizes, both optical modes, missing the initial two seconds, layouts larger than 32 packets, duplicate clicks, an immutable snapshot and single application after redundant passes. The final corner checks also passed independently. No UI or macOS tests, paid API calls or model benchmarks were needed for these transport changes. Actual first-click synchronization in the game remains pending acceptance of the updated EXE and addon together.
