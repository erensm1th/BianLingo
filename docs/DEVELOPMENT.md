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
