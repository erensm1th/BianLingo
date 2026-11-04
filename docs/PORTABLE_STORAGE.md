# Portable storage

Windows stores application-managed data in `UserData` beside `BianLingo.exe`:

- settings and encrypted connection profiles;
- optional daily chat logs;
- language models, model attribution and downloads;
- temporary files and application-managed caches.

The Windows archive contains `BianLingo.exe`, `data` and `AddOns`. Copy
`AddOns/BianLingo` into the game's `Interface/AddOns` directory. Runtime
components, bundled models and matching source remain in `data`; full notices
are in `data/Notices`. User settings and downloaded models remain in `UserData`.

Initialization verifies the directory is writable and preserves existing files. An invalid location causes a visible error instead of switching to a system data directory. Keep the application in a folder you can write to. Do not share `UserData` with another player.

Ollama uses an application-owned `127.0.0.1:11435` service and private model, profile and temporary directories. The application manages only its own child process, reports a conflicting port, and sets `OLLAMA_NO_CLOUD=1`.

Invalid configuration is recovered per field. The original bytes are backed up as `UserData/settings.recovery-<time>-<random>.json` before any replacement; failure to preserve that backup prevents overwriting the file. Backups can contain private information.
