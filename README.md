<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

**Live World of Warcraft chat translation.** Read incoming chat in your language and translate your replies in a compact overlay.

**English** · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [한국어](README.ko.md)<br>
[Français](README.fr.md) · [Deutsch](README.de.md) · [Español](README.es.md) · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

[Build from source](docs/BUILDING.md) · [Privacy](PRIVACY.md)

BianLingo is a free, non-commercial fan project. It is not affiliated with, endorsed or approved by Blizzard Entertainment.

## Features

- **Local or online translation.** Use Bergamot, CTranslate2, or Ollama on your PC, or configure an API endpoint, model, and key. OpenAI-compatible, Anthropic, and Gemini request formats are available.
- **Prepare languages before playing.** Select your target language, review the required language pairs, and download the models you need. Supported languages depend on the chosen engine and model.
- **A chat overlay that fits your game.** Resize and move the window, adjust background transparency and text size, choose a skin, or load a custom skin.
- **Your own chat tabs.** Name tabs, choose message types, and import the chat layout exported by the addon.
- **Reply with a click.** Left-click a message to prepare a reply in its channel. Right-click for whisper, a manual level-query command, or blocking. Press Enter to translate and copy; paste the text into the game yourself.
- **Control what gets translated.** Choose channels, skip noise and target-language nicknames, and protect game links and terminology. Incoming and outgoing prompts can be edited separately.
- **Manual replay.** The addon caches 10 complete messages by default, with a configurable limit of 1–100. Replay missed messages using the minimap button or `/bl replay`; duplicates do not trigger another translation.
- **Compact transport and OCR.** Choose a small color matrix or a binary pixel grid. Optional idle hiding keeps a tiny status marker. On Windows, a selected chat region can also be read with OCR.
- **Optional chat logs.** Logging is off by default. When enabled, daily UTF-8 logs include received messages and submitted output translations.
- **Portable data.** Settings, logs, downloaded models, and application-managed caches stay in `UserData` next to `BianLingo.exe`.

## Translation options

| Backend | Capabilities | Constraints | Use when |
|---|---|---|---|
| **Online API** | Prompts and styles; no model download | Network, quotas, usage fees and provider data policies | Prompt control without local inference |
| **Ollama** | Local LLM; prompts and styles | Runtime/model downloads; hardware-dependent memory and latency | Local processing with prompt control |
| **Bergamot** | Offline CPU translation; no API usage fees | Prepared language pairs; no prompts | Offline translation with a supported pair |
| **CTranslate2** | Offline CPU INT8 translation; no API usage fees | Prepared OPUS-MT pairs; no prompts | Offline translation with an OPUS-MT pair |

Bergamot and CTranslate2 automatic chat routing assumes English input, or Chinese input when translating into English; downloading more pairs does not enable arbitrary source-language detection. Local processing does not send chat to a translation provider. Actual latency and output quality depend on the selected model, language pair, message and hardware/network conditions.

### Benchmark · AMD Ryzen 7 9700X

| Backend | EN→ZH / ZH→EN median | Peak RAM | Sustained CPU | Valid responses | Output observations |
|---|---:|---:|---:|---:|---|
| **Bergamot** | **13 / 19 ms** | **439 MiB** | **3.3%** | 72/72 | Lowest latency; some game abbreviations and terms mistranslated |
| CTranslate2 | 64 / 90 ms | 450 MiB | 18.1% | 72/72 | Low latency; game terminology errors |
| Qwen3 0.6B | 181 / 99 ms | 1.09 GiB | 49.6% | 69/72 | Omissions and copied prompt examples |
| Qwen3 1.7B | 479 / 515 ms | 1.89 GiB | 49.6% | 72/72 | Complete responses, but substantial mistranslations |

CPU-only English ↔ Simplified Chinese engine benchmarks. CPU shows sustained translation load, not idle usage. Valid responses are not accuracy scores. Capture, OCR, UI and terminology shortcuts are excluded.

### API starting configurations

These examples use ordinary saved connections. They do not change the UI language or translation target. DeepSeek Flash supports non-thinking requests; Google lists translation among Gemini 3.5 Flash-Lite’s workloads. Provider documentation: [DeepSeek](https://api-docs.deepseek.com/), [Gemini model](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite) and [Gemini translation/pricing](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite).

| Setting | DeepSeek | Google Gemini |
|---|---|---|
| Base URL | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| Model | `deepseek-flash` | `gemini-3.5-flash-lite` |
| API key | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| Extra parameters | `{"thinking":{"type":"disabled"}}` | `{}` (model defaults) |

For both examples, add a connection under **API & usage**, use **OpenAI Chat Completions** (`openai_chat`), **Auto** authentication and leave **Full URL** off. Enter your own API key. In advanced settings, set the extra parameters shown above; leave custom headers empty and balance lookup disabled. Save, then test the connection. See [connection settings](docs/PROVIDERS.md). The Gemini URL is Google’s documented [OpenAI-compatible endpoint](https://aistudio.google.com/docs/openai).

Model availability, prices, quotas and data handling are controlled by each provider: [DeepSeek pricing](https://api-docs.deepseek.com/quick_start/pricing/), [Gemini pricing and data use](https://aistudio.google.com/docs/pricing), [Gemini availability](https://aistudio.google.com/docs/available-regions). Gemini’s free and paid tiers have different limits and data-use terms. Configuration details follow the linked provider documentation.

## How it works

1. The **BianLingo addon** reads chat through the game's addon API and briefly displays encoded pixels in a corner of the game window.
2. The **BianLingo desktop app** decodes those pixels, translates the text using your selected connection, and displays it in the overlay.

The app does not read game memory, inject code, or send chat automatically. OCR captures the chat region you select. If Blizzard hides chat from addons, the pixel bridge cannot recover that hidden text; OCR is an optional input path.

## Requirements

| Platform | Status |
|---|---|
| Windows 10/11 x64 | Available. Window OCR requires Windows 10 1903 or later. |
| macOS | Under development. Planned for a future release; no macOS build is available yet. |

- World of Warcraft with the BianLingo addon, in windowed or borderless-windowed mode.
- A local language model, or an API connection you provide. The full Windows package includes Bergamot English ↔ Simplified Chinese models; other available language pairs can be prepared separately.

The interface supports English, Simplified Chinese, Japanese, Korean, French, German, Spanish, Brazilian Portuguese and Russian. Select **Interface language** in **Chat appearance**, then save to apply immediately; **Follow system** is the default, with English fallback for unsupported locales. Chinese locales use the Simplified Chinese UI. Interface language, README language and translation targets are independent; model language coverage depends on the engine and model. Errors and status notifications use Chinese in the Chinese interface and English in every other interface language.

The addon declares support for Classic Era and compatible Classic/Retail clients. Chat availability depends on the client and scene. HDR capture, mixed-DPI displays, and game-client combinations need environment-specific testing.

## Quick start

1. **Install the addon.** Copy the package's `AddOns/BianLingo` folder into `World of Warcraft/<client>/Interface/AddOns/` (for example, `<client>` is `_retail_`, `_classic_era_` or `_forever_`). The final path must end in `Interface/AddOns/BianLingo/BianLingo.toc`. Enable BianLingo in the game's AddOns list, then run `/reload`.
2. **Start and position the overlay.** Keep `BianLingo.exe` and `data` together and launch the EXE. **Click the top-right cat icon to open settings; drag the same icon to move the overlay.**
3. **Choose translation.** Under **API & usage**, select a connection. **Bergamot / CTranslate2:** choose the chat target language and use **Prepare current language pack**, or **Manage language packs…** to download pairs in advance. **Ollama:** choose and download a model; no API key is needed. **Online API:** choose or add a connection, enter the provider's base URL, model and API key, save and test it. Starter values are listed above.
4. **Choose what to translate.** Set **Chat translation target language** under **API & usage**. Under **Translation scope**, select the channels to translate, then **Save settings**. Local translation requires a supported, prepared language pair.
5. **Translate a reply.** Press the **· / backtick (&#96;) key, normally below Esc**, to open the output box. Enter text and press **Enter** to translate and copy the result; paste it into the game yourself. Under **Output translation**, set the output target/style or click the shortcut field, press a new key combination and **Save settings**. The incoming-chat and output targets are separate.
6. **Copy the game's chat layout.** Configure the chat windows in WoW first. With the EXE running, enter `/bl windows`, or select **Synchronize chat windows** in `/bl settings`. A complete received layout automatically **replaces** the overlay tabs; no separate import or apply step is needed. The addon must be enabled for synchronization.


The addon and desktop app work together. Install both from the same package. No Python installation is required to run the Windows application. To receive Chinese chat for translation into another language, run `/bl cn off` if the addon’s Chinese filter is enabled.

### Windows SmartScreen

Windows may show “Windows protected your PC” for this unsigned release. This is Microsoft Defender SmartScreen’s signature and download-reputation check; new or unsigned files may be unrecognized, and signing alone does not guarantee that warnings disappear. See Microsoft’s official explanation. [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## Documentation

| Topic | Guide |
|---|---|
| Translation connections | [Providers](docs/PROVIDERS.md) |
| Local models | [Local translation](docs/LOCAL_TRANSLATION.md) |
| Message actions and blocking | [Message actions](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) |
| Chat tabs | [Chat tabs](docs/CHAT_TABS.md) |
| Themes and custom skins | [Skins](docs/SKINS.md) |
| OCR input | [OCR](docs/OCR_FALLBACK.md) |
| Building and packaging | [Building](docs/BUILDING.md) |

English is the default language for project guides and notices. Translated READMEs are available through the language links above.

## Privacy and license

Local translation runs on your PC. An online connection sends submitted text to the provider you configure; its terms and charges apply. Logs can contain player names and private messages. See [Privacy](PRIVACY.md).

Project code is licensed under [MIT](LICENSE). Dependencies, models, and terminology retain their own terms and attribution; see [Third-party notices](THIRD_PARTY_NOTICES.md). World of Warcraft and Blizzard Entertainment are trademarks of their respective owners.

All public downloads and features are available without donating. Contact for project, privacy or licensing questions: [@erensm1th](https://github.com/erensm1th).
