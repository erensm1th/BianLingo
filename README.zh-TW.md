<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

[English](README.md) · [简体中文](README.zh-CN.md) · **繁體中文** · [日本語](README.ja.md) · [한국어](README.ko.md)<br>
[Français](README.fr.md) · [Deutsch](README.de.md) · [Español](README.es.md) · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

**魔獸世界即時聊天翻譯工具。** 以選擇的語言閱讀聊天，並在精簡浮窗中翻譯回覆。

[從原始碼建置](docs/BUILDING.md) · [隱私說明](PRIVACY.md)

BianLingo 是免費、非商業營運的魔獸世界粉絲專案，與 Blizzard Entertainment 沒有隸屬關係，也未經其認可或批准。

## 主要功能

- **本機或線上翻譯**：選擇 Bergamot、CTranslate2、Ollama，或自行填寫 API 位址、模型與金鑰。支援 OpenAI 相容、Anthropic 和 Gemini 請求格式。
- **進入遊戲前準備模型**：依目標語言確認並下載所需語言配對；實際支援範圍取決於引擎與模型。
- **聊天浮窗**：支援移動、縮放、背景透明度、字型大小、主題和自訂外觀。
- **聊天分頁**：獨立命名及篩選訊息類型，或匯入插件匯出的遊戲聊天配置。
- **點擊回覆**：左鍵準備在訊息所屬頻道回覆；右鍵選單提供密語、手動等級查詢及封鎖。Enter 翻譯並複製，由你貼到遊戲中送出。
- **翻譯範圍**：篩選頻道、雜訊和使用目標語言文字系統的暱稱。保護連結與遊戲術語，收訊與輸出提示詞分開設定。
- **手動補發**：預設在插件記憶體保留最近 10 則完整訊息，可設為 1–100 則。右鍵小地圖按鈕或 `/bl replay` 補發；重複訊息不再次翻譯。
- **像素傳輸與 OCR**：選擇小型彩色矩陣或二值網格，可在閒置時隱藏資料區並保留微型狀態標記。Windows 可讀取自行選取的聊天區域進行 OCR。
- **選用記錄**：預設關閉；啟用後，將收到的訊息與已提交的輸出翻譯追加到每日 UTF-8 記錄。
- **可攜式儲存**：設定、記錄、下載模型及程式管理的快取存放於 `BianLingo.exe` 同目錄的 `UserData`。

## 翻譯方式比較

| 方式 | 能力 | 限制 | 適用情境 |
|---|---|---|---|
| **線上 API** | 提示詞與輸出風格，無須下載模型 | 網路、配額、使用費用及服務商資料條款 | 不執行本機推論的提示詞翻譯 |
| **Ollama** | 本機大型語言模型，支援提示詞與風格 | 需下載執行環境和模型；記憶體與耗時取決於硬體 | 需要提示詞控制的本機處理 |
| **Bergamot** | 離線 CPU 翻譯，無 API 使用費用 | 需準備語言配對，不使用提示詞 | 支援語言配對的離線翻譯 |
| **CTranslate2** | 離線 CPU INT8 翻譯，無 API 使用費用 | 需準備 OPUS-MT 語言配對，不使用提示詞 | OPUS-MT 語言配對的離線翻譯 |

Bergamot、CTranslate2 自動收訊假設原文是英語，目標為英語時依漢字規則選擇中文來源；下載更多語言包不代表能自動辨識任意來源語言。本機處理不會將聊天傳送給翻譯服務商。實際延遲與譯文取決於模型、語言配對、訊息及硬體／網路條件。

### Benchmark · AMD Ryzen 7 9700X

| 翻譯方案 | 英→中 / 中→英耗時中位數 | 峰值記憶體 | 連續翻譯 CPU | 有效回傳 | 譯文表現 |
|---|---:|---:|---:|---:|---|
| **Bergamot** | **13 / 19 ms** | **439 MiB** | **3.3%** | 72/72 | 最快；部分遊戲縮寫、術語誤譯 |
| CTranslate2 | 64 / 90 ms | 450 MiB | 18.1% | 72/72 | 速度較快；遊戲術語仍易錯 |
| Qwen3 0.6B | 181 / 99 ms | 1.09 GiB | 49.6% | 69/72 | 出現漏譯、照抄提示詞範例 |
| Qwen3 1.7B | 479 / 515 ms | 1.89 GiB | 49.6% | 72/72 | 回傳穩定，但仍有明顯誤譯 |

僅測試 CPU 下的英語 ↔ 簡體中文引擎翻譯。CPU 為連續翻譯負載，不是閒置用量；有效回傳不代表翻譯正確。數據不含擷取畫面、OCR、介面繪製及術語詞庫快速比對。

### API 起始設定

以下是一般自訂連線範例，不會變更介面語言或翻譯目標。DeepSeek Flash 支援非思考請求；Google 將翻譯列為 Gemini 3.5 Flash-Lite 的適用工作。官方文件：[DeepSeek](https://api-docs.deepseek.com/)、[Gemini 模型](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite)、[Gemini 翻譯工作與計費](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite)。

| 設定項目 | DeepSeek | Google Gemini |
|---|---|---|
| 基礎位址 | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| 模型 | `deepseek-flash` | `gemini-3.5-flash-lite` |
| API 金鑰 | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| 額外參數 | `{"thinking":{"type":"disabled"}}` | `{}` (模型預設值) |

在 **API & usage → Add** 新增連線，使用 **OpenAI Chat Completions**（`openai_chat`）、**Auto** 驗證，關閉 **Full URL**，填入自己的 API 金鑰。在進階設定套用表中的額外參數，自訂標頭留空、餘額查詢維持關閉；儲存後測試連線。詳見[連線設定](docs/PROVIDERS.md)與 Google 官方的 [OpenAI 相容介面](https://aistudio.google.com/docs/openai)。

模型可用性、價格、配額和資料處理由服務商決定：[DeepSeek 計費](https://api-docs.deepseek.com/quick_start/pricing/)、[Gemini 計費與資料使用](https://aistudio.google.com/docs/pricing)、[Gemini 可用地區](https://aistudio.google.com/docs/available-regions)。Gemini 免費與付費層級有不同的限制和資料使用條款。

## 運作方式

1. **BianLingo 插件**透過遊戲插件 API 讀取聊天，並在遊戲角落短暫顯示編碼像素。
2. **桌面程式**解碼像素，以選取的連線翻譯文字，再顯示於浮窗。

程式不讀取遊戲記憶體、不注入程式碼，也不自動傳送聊天。OCR 擷取你選取的區域。若 Blizzard 對插件隱藏聊天正文，像素傳輸無法還原該文字；OCR 是另一條輸入途徑。

## 系統需求與語言

| 平台 | 狀態 |
|---|---|
| Windows 10/11 x64 | 目前可用；視窗 OCR 要求 Windows 10 1903 以上。 |
| macOS | 開發中，預計於後續版本支援；目前尚無 macOS 安裝包。 |

- 已安裝 BianLingo 插件的魔獸世界，以視窗或無邊框視窗模式執行。
- 本機模型或自行提供的 API 連線。完整套件包含 Bergamot 英語 ↔ 簡體中文模型，其他可用語言配對可另行準備。

介面支援英語、簡體中文、日語、韓語、法語、德語、西班牙語、巴西葡萄牙語和俄語。在**聊天外觀 → 介面語言**中選擇，儲存後立即生效；預設**跟隨系統**，未支援的系統語言回退至英語。中文語言環境使用簡體中文介面。介面語言、README 語言與翻譯目標相互獨立；翻譯範圍取決於引擎和模型。 錯誤與執行通知提供中英兩版：中文介面顯示中文，其餘介面顯示英語。

插件宣告支援 Classic Era 及相容的 Classic／Retail 用戶端。聊天是否開放取決於用戶端與場景；HDR、多螢幕混合 DPI 及不同遊戲版本須在各環境確認。

## 快速上手

1. **安裝遊戲插件。** 將包內 `AddOns/BianLingo` 複製到 `World of Warcraft/<用戶端>/Interface/AddOns/`，例如 `_retail_`、`_classic_era_` 或 `_forever_`。最終應有 `Interface/AddOns/BianLingo/BianLingo.toc`。在遊戲插件清單啟用 BianLingo，再執行 `/reload`。
2. **啟動與移動浮窗。** 保持 `BianLingo.exe` 與 `data` 在同一目錄，執行 EXE。**點擊右上角貓咪圖示開啟設定；按住該圖示拖曳即可移動窗口。**
3. **選擇翻譯方式。** 在 **API 與餘額**選擇連線。**Bergamot / CTranslate2：**選擇聊天目標語言，點擊準備目前語言包，或開啟語言包管理提前下載。**Ollama：**選擇並下載模型，無需 API Key。**線上 API：**選擇或新增連線，填入 Base URL、模型名稱與 API Key，儲存後測試；上方表格提供初始設定。
4. **設定接收翻譯。** 在 API 頁面選擇聊天翻譯目標語言，在翻譯範圍勾選頻道，再儲存設定。本機翻譯須支援並已準備對應語言對。
5. **翻譯要發送的話。** 預設按 **Esc 下方的 · / 反引號鍵（&#96;）**開啟輸出框，輸入後按 **Enter** 翻譯並複製，再自行貼到遊戲發送。在輸出翻譯設定中選擇目標語言與風格；點擊快捷鍵欄位、按下新的組合鍵，再儲存。輸出與接收翻譯的目標語言獨立設定。
6. **同步遊戲聊天配置。** 先在遊戲設定聊天窗口及頻道。保持 EXE 執行，輸入 `/bl windows`，或在 `/bl settings` 點擊同步聊天窗口。完整接收後自動**取代**浮窗頁籤，無需再次匯入或套用；同步時須啟用插件。


程式與插件需來自同一套件。將中文聊天翻成其他語言時，若插件的中文篩選已啟用，請在遊戲執行 `/bl cn off`。

### Windows SmartScreen 提示

目前版本未簽署，Windows 可能顯示「Windows 已保護您的電腦」。這是 Microsoft Defender SmartScreen 的簽章與下載信譽檢查機制；新發佈或未簽署的檔案可能尚未被識別，即使簽署也不保證提示立即消失。詳見 Microsoft 官方說明。 [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## 文件

[翻譯連線](docs/PROVIDERS.md) · [本機模型](docs/LOCAL_TRANSLATION.md) · [聊天分頁](docs/CHAT_TABS.md) · [訊息操作與封鎖](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [外觀](docs/SKINS.md) · [OCR](docs/OCR_FALLBACK.md) · [建置](docs/BUILDING.md)

詳細指南與專案說明預設使用英語；各語言 README 可從頁首連結選擇。

## 隱私與授權

本機翻譯在你的電腦執行；線上連線會將原文傳送至你設定的服務商，並適用其條款及費用。記錄可能包含玩家名稱和私人訊息，詳見[隱私說明](PRIVACY.md)。

專案程式碼採用 [MIT](LICENSE)。第三方元件、模型與術語各自保留其條款與來源標示，詳見[第三方聲明](THIRD_PARTY_NOTICES.md)。World of Warcraft 和 Blizzard Entertainment 商標屬於各自權利人。

所有公開下載與功能均無需贊助。專案、隱私或授權問題的聯絡入口：[@erensm1th](https://github.com/erensm1th)。
