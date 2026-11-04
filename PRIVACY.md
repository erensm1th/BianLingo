# Privacy and local data

Updated: 2026-11-04

I maintain BianLingo as an independent fan project. I'm not affiliated with Blizzard, model publishers or API providers, and their names do not imply endorsement. The addon is free and open source, with no ads or activation required. See [Project notice](PROJECT_NOTICE.md) for licensing and voluntary support.

## Capture and game interaction

The addon reads available chat through the game's APIs and displays encoded pixels. The desktop app decodes those pixels or, when OCR is enabled, recognizes the selected game-chat region. Window capture/location may temporarily process a larger target-game image. Normal translation does not automatically save or upload screenshots. Recognized text follows your selected local or online translation connection.

On Windows, the app locates the game through window APIs without opening a game-process handle or querying its executable path. It does not read/write game memory, inject code or automatically paste/send chat. When you request a reply, it can copy text and restore game focus; you paste and send the message yourself. Clipboard history and cross-device synchronization follow your operating-system settings.

Your blocklist stays in the app's data directory. Copy its data-only sync code and import it in the addon; WoW saves the list through SavedVariables. This reads/writes no game-directory, addon Lua or WTF files and needs no game path. Install the addon manually.

## Local and remote translation

Local Bergamot, CTranslate2 and local Ollama inference process chat on the device. Runtime/model/OCR-data downloads still contact hosting services and disclose ordinary network metadata, including IP addresses.

Online translation sends the text you submit, your configured prompts and the required authentication to the service you choose. Custom prompts, headers and request options may add information. Your provider's privacy terms and plan apply. I don't verify custom endpoints or models; choose a service you trust. Do not include API keys or other secrets in text to translate. API charges are between users and providers, separate from voluntary project support.

## Storage

On Windows, settings, encrypted connection credentials, downloaded models, OCR data, temporary files and logs live in `UserData` next to `BianLingo.exe`. Portable runtimes live in `data`. Protect these directories and exclude personal `UserData` from shared packages. The macOS adapter uses its application data directory; macOS support is under development.

Explicit model preparation may record the model name, digest and publisher-provided license locally. These records contain model metadata, not chat. Keep encrypted settings and recovery backups private too.

## Chat logs and controls

Logging is **off by default**; a saved logging preference is retained. When enabled, the journal records received text, player names/realms, channels, original/receive times, replay status and processing outcomes. Recording happens before desktop channel filters, so it may include unselected channels and whispers. Submitted output translations are recorded too; they do not prove the text was sent to the game. The journal does not record drafts, keystrokes or saved credentials.

**Settings → Status** provides a logging switch, retention in days (zero keeps logs until manual deletion), an open-folder action and deletion with confirmation. Disabling stops new entries; an in-progress write may finish. Disabling alone does not remove earlier logs unless retention is configured.

Deletion is asynchronous and covers only regular `chat-YYYY-MM-DD.log` files in the log directory, not unrelated diagnostics or linked files. If logging remains enabled, later entries may create a new file. Retention runs on startup, policy changes and local-date rotation without recurring filesystem scans. Write/cleanup errors remain visible in Status.

Respect other players’ privacy when recording or sharing chat. Logging controls do not replace applicable privacy and data-processing obligations.

## Diagnostics and contact

The application does not implement analytics uploads, a license/activation server or an automatic application updater. Runtime diagnostics may be stored locally. Third-party services, runtimes and operating systems have their own policies. Review and redact logs before sharing.

For privacy, copyright or licensing questions, you can find me on GitHub as [@erensm1th](https://github.com/erensm1th). Include the app version and a short description. Please keep API keys, private chat and credentials out of public reports.
