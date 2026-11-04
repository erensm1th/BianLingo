<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

**魔兽世界实时聊天翻译工具。** 用熟悉的语言阅读聊天，在简洁浮窗中翻译回复。

[English](README.md) · **简体中文** · [繁體中文](README.zh-TW.md) · [日本語](README.ja.md) · [한국어](README.ko.md)<br>
[Français](README.fr.md) · [Deutsch](README.de.md) · [Español](README.es.md) · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

[源码构建](docs/BUILDING.md) · [隐私说明](PRIVACY.md)

BianLingo 是一个免费、非商业运营的魔兽世界粉丝项目，与暴雪没有隶属关系，也未经暴雪认可或批准。

## 主要功能

- **本地或在线翻译**：选择 Bergamot、CTranslate2、Ollama，或自行填写 API 地址、模型与密钥；支持 OpenAI 兼容、Anthropic 和 Gemini 请求格式。
- **进游戏前准备模型**：按目标语言准备所需语言对，也可以搜索和手动选择。实际支持的语言取决于引擎与模型。
- **贴近游戏的聊天浮窗**：支持拖动、缩放、背景透明度、字号、多种皮肤和自定义皮肤。
- **独立聊天页签**：为页签命名、选择消息类型，或导入插件导出的游戏聊天布局。
- **点击回复**：左键回复消息所在频道，右键选择私聊、手动等级查询或屏蔽。回车翻译并复制，由你粘贴到游戏中发送。
- **灵活过滤**：按频道筛选，跳过噪声与目标语言昵称，保护物品链接和游戏术语；聊天翻译与输出翻译的提示词分别配置。
- **手动补发**：默认缓存最近 10 条完整消息，上限可设为 1–100 条。右键小地图按钮或输入 `/bl replay` 补发；重复消息不再次请求翻译。
- **两种数据条与 OCR**：可选小矩阵或二值网格，支持空闲隐藏和微型状态标记。Windows 可使用自行选定的聊天区域进行 OCR 输入。
- **可选日志**：默认关闭；开启后按日追加 UTF-8 聊天与已提交输出翻译日志。
- **便携存储**：设置、日志、下载模型及程序管理的缓存保存在 `BianLingo.exe` 同目录的 `UserData` 中。

## 翻译方式对比

| 方式 | 能力 | 限制 | 适用场景 |
|---|---|---|---|
| **在线 API** | 提示词与输出风格，无需下载模型 | 网络、配额、使用费用及服务商数据条款 | 不运行本地模型的提示词翻译 |
| **Ollama** | 本地大语言模型，支持提示词与风格 | 下载运行环境和模型，内存与耗时取决于硬件 | 需要提示词控制的本地处理 |
| **Bergamot** | 离线 CPU 翻译，无 API 使用费用 | 需准备语言对，不使用提示词 | 支持语言对的离线翻译 |
| **CTranslate2** | 离线 CPU INT8 翻译，无 API 使用费用 | 需准备 OPUS-MT 语言对，不使用提示词 | OPUS-MT 语言对的离线翻译 |

Bergamot、CTranslate2 自动收聊采用英语来源假设，目标为英语时按汉字规则选择中文来源；下载更多语言包不等于自动识别任意来源语言。本地处理不会把聊天发送给翻译服务商。实际耗时与译文取决于模型、语言对、消息及硬件／网络条件。

### Benchmark · AMD Ryzen 7 9700X

| 翻译方式 | 调用耗时中位数 | 每秒 1 条时 CPU | 本机工作集 |
|---|---:|---:|---:|
| Bergamot | 20.9 ms | 0.10% | 337.5 MiB |
| CTranslate2 · Ruy CPU INT8 | 67.2 ms | 1.36% | 157.3 MiB |
| DeepSeek · `deepseek-chat` | 708.2 ms | 0.01% | 23.4 MiB |

### API 起始配置

以下示例均使用普通的自定义连接，不改变界面语言或翻译目标。DeepSeek Flash 支持非思考请求；Google 将翻译列为 Gemini 3.5 Flash-Lite 的适用任务。官方文档：[DeepSeek](https://api-docs.deepseek.com/)、[Gemini 模型](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite)、[Gemini 翻译任务与计费](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite)。

| 设置项 | DeepSeek | Google Gemini |
|---|---|---|
| 基础地址 | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| 模型 | `deepseek-flash` | `gemini-3.5-flash-lite` |
| API Key | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| 额外参数 | `{"thinking":{"type":"disabled"}}` | `{}`（模型默认值） |

两者均在 **API 与余额 → 新增** 中配置：协议使用 **OpenAI Chat Completions**（`openai_chat`），鉴权选择 **自动**，关闭 **完整 URL**，填写自己的 API Key。在高级设置中填写表内额外参数，自定义请求头留空、余额查询保持关闭；保存后测试连接。详见 [连接设置](docs/PROVIDERS.md)。Gemini 地址采用 Google 官方提供的 [OpenAI 兼容接口](https://aistudio.google.com/docs/openai)。

模型可用性、价格、配额和数据处理由服务商决定：[DeepSeek 计费](https://api-docs.deepseek.com/quick_start/pricing/)、[Gemini 计费与数据使用](https://aistudio.google.com/docs/pricing)、[Gemini 可用地区](https://aistudio.google.com/docs/available-regions)。Gemini 免费与付费层级的限制和数据使用条款不同。配置字段依据所链接的服务商文档。

## 工作方式

1. **BianLingo 插件**通过游戏插件 API 读取聊天，在游戏角落短暂显示编码像素。
2. **桌面程序**解码像素，使用所选连接翻译，并在浮窗显示结果。

程序不读取游戏内存、不注入代码，也不自动发送聊天。OCR 捕获你选定的区域；暴雪对插件隐藏的聊天正文不能通过像素传输恢复，OCR 可作为独立输入路径。

## 快速上手

1. **安装游戏插件。** 将包内 `AddOns/BianLingo` 文件夹复制到 `World of Warcraft/<客户端>/Interface/AddOns/`，客户端目录例如 `_retail_`、`_classic_era_` 或 `_forever_`。最终应能找到 `Interface/AddOns/BianLingo/BianLingo.toc`。在游戏插件列表中启用 BianLingo，再执行 `/reload`。
2. **启动与移动浮窗。** 保持 `BianLingo.exe` 与 `data` 文件夹在同一目录，双击 EXE。**单击浮窗右上角猫咪图标打开设置；按住该图标拖动即可移动窗口。**
3. **切换翻译方式。** 在**API 与余额**中选择翻译连接。**Bergamot / CTranslate2：**选择聊天目标语言，点击**准备当前语言包**，也可在**管理语言包**中提前下载语言对。**Ollama：**选择并下载模型，无需 API Key。**在线 API：**选择或新增连接，填写服务商的 Base URL、模型名和 API Key，保存后测试连接；上方表格提供初始配置。
4. **设置接收翻译。** 在**API 与余额 → 聊天翻译目标语言**选择希望读到的语言，在**翻译范围**勾选需要翻译的频道，点击**保存设置**。本地翻译需要对应语言对受支持且已准备好。
5. **翻译要发送的话。** 默认按 **Esc 下方的 · / 反引号键（&#96;）**呼出输出翻译框，输入文字后按 **Enter**，结果会自动复制；自行粘贴到游戏发送。在**输出翻译**中可选择输出目标语言、风格；点击快捷键输入框，按下新的按键组合，再点**保存设置**即可更改快捷键。输出目标与接收聊天的目标语言分别设置。
6. **同步游戏聊天布局。** 先在魔兽中配置聊天窗口名称和显示频道，保持 EXE 运行，在游戏输入 `/bl windows`，或在 `/bl settings` 中点击**同步聊天窗口**。完整接收后自动**替换**浮窗页签，无需再导入或应用；同步时须保持插件开启。


客户端与插件需要配套使用。将中文聊天翻译成其他语言时，如果插件的中文过滤已开启，请在游戏执行 `/bl cn off`。完整包包含 Bergamot 英语与简体中文双向模型；其他可用语言对可单独准备。

### Windows SmartScreen 提示

当前版本未签名，Windows 可能显示“Windows 已保护你的电脑”。这是微软 Defender SmartScreen 的签名与下载信誉检查机制；新发布或未签名文件可能尚未被识别，即使签名也不保证提示立即消失。详见微软官方说明。 [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## 系统与语言

| 平台 | 状态 |
|---|---|
| Windows 10/11 x64 | 当前可用；窗口 OCR 要求 Windows 10 1903 及以上。 |
| macOS | 正在开发，计划在后续版本支持；目前尚无 macOS 安装包。 |

游戏使用窗口或无边框窗口模式。

插件声明支持经典旧世及相应 Classic／正式服（Retail）客户端。聊天是否对插件开放取决于游戏客户端和场景；暴雪隐藏的聊天正文不能通过像素桥接恢复，可按需要启用 OCR。HDR、多屏混合 DPI 和不同游戏版本需按具体环境验收。

界面支持英语、简体中文、日语、韩语、法语、德语、西班牙语、巴西葡萄牙语和俄语。在**聊天外观 → 界面语言**中选择，保存后立即生效；默认**跟随系统**，未支持的系统语言回退到英语。中文语言环境使用简体中文界面。界面语言、README 语言和翻译目标相互独立；翻译覆盖范围取决于引擎与模型。 错误与运行通知采用中英两版：中文界面显示中文，其余界面显示英语。

## 文档

[翻译连接](docs/PROVIDERS.md) · [本地模型](docs/LOCAL_TRANSLATION.md) · [聊天页签](docs/CHAT_TABS.md) · [屏蔽名单](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [皮肤](docs/SKINS.md) · [源码构建](docs/BUILDING.md)

详细指南与项目说明默认使用英语；各语言 README 可从页首语言入口选择。

## 隐私与许可

本地模型在电脑上翻译；在线连接会把提交的文字发送给你配置的服务商，相关费用与条款由服务商决定。日志可能包含昵称与私聊，详见 [隐私说明](PRIVACY.md)。

项目代码采用 [MIT](LICENSE)。第三方组件、模型与术语遵循各自条款，详见 [第三方声明](THIRD_PARTY_NOTICES.md)。魔兽世界及暴雪相关商标属于相应权利人。

所有公开下载和功能均无需赞助。项目、隐私或许可问题的联系入口：[@erensm1th](https://github.com/erensm1th)。
