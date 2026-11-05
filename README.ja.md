<p align="center"><img src="resources/branding/logo.png" width="140" alt="BianLingo"></p>

# BianLingo

[English](README.md) · [简体中文](README.zh-CN.md) · [繁體中文](README.zh-TW.md) · **日本語** · [한국어](README.ko.md)<br>
[Français](README.fr.md) · [Deutsch](README.de.md) · [Español](README.es.md) · [Português (Brasil)](README.pt-BR.md) · [Русский](README.ru.md)

**World of Warcraft のリアルタイムチャット翻訳。** チャットを選択した言語で読み、コンパクトなオーバーレイから返信を翻訳できます。

[ソースからビルド](docs/BUILDING.md) · [プライバシー](PRIVACY.md)

BianLingo は無料・非営利のファンプロジェクトです。Blizzard Entertainment との提携関係はなく、同社の承認・推奨を受けていません。

## 主な機能

- **ローカル／オンライン翻訳**：Bergamot、CTranslate2、Ollama、または API の URL・モデル・キーを指定した接続を使用できます。OpenAI 互換、Anthropic、Gemini のリクエスト形式に対応しています。
- **プレイ前にモデルを準備**：翻訳先の言語に必要な言語ペアを確認してダウンロードできます。対応言語はエンジンとモデルによって異なります。
- **チャットオーバーレイ**：移動、サイズ変更、背景の透明度、文字サイズ、テーマ、カスタムスキンを設定できます。
- **チャットタブ**：タブ名と表示するメッセージ種別を指定したり、アドオンから出力したゲーム内チャットの構成を取り込んだりできます。
- **クリックして返信**：左クリックで元のチャンネルへの返信を準備します。右クリックメニューにはささやき、手動のレベル照会、ブロックがあります。Enter で翻訳・コピーし、ゲームへの貼り付けと送信は自分で行います。
- **翻訳対象の絞り込み**：チャンネル、ノイズ、翻訳先言語と同じ文字体系の名前をフィルターできます。リンクとゲーム用語を保護し、受信と返信のプロンプトを別々に編集できます。
- **手動再送**：アドオンは既定で直近 10 件の完全なメッセージをメモリに保存し、上限を 1–100 件に変更できます。ミニマップボタンの右クリックまたは `/bl replay` で再送します。重複は再翻訳しません。
- **ピクセル転送と OCR**：小型カラーマトリクス／二値グリッドを選択できます。アイドル時はデータ部分を非表示にし、小さな状態マーカーだけを残せます。Windows では指定したチャット領域の OCR も使用できます。
- **任意のログ**：初期状態では無効です。有効にすると、受信メッセージと確定した出力翻訳を日別の UTF-8 ログへ追記します。
- **ポータブル保存**：設定、ログ、ダウンロードしたモデル、アプリ管理のキャッシュは `BianLingo.exe` と同じフォルダーの `UserData` に保存します。

## 翻訳方式の比較

| 方式 | 機能 | 制約 | 用途 |
|---|---|---|---|
| **オンライン API** | プロンプトと出力スタイル。モデルのダウンロード不要 | ネットワーク、利用枠、料金、提供元のデータ規約 | ローカル推論なしでプロンプトを使う翻訳 |
| **Ollama** | ローカル LLM。プロンプトとスタイルに対応 | ランタイムとモデルの準備が必要。メモリと速度はハードウェア依存 | プロンプトを使うローカル処理 |
| **Bergamot** | オフライン CPU 翻訳。API 使用料なし | 言語ペアの準備が必要。プロンプト非対応 | 対応する言語ペアのオフライン翻訳 |
| **CTranslate2** | オフライン CPU INT8 翻訳。API 使用料なし | OPUS-MT 言語ペアの準備が必要。プロンプト非対応 | OPUS-MT 言語ペアのオフライン翻訳 |

Bergamot と CTranslate2 の自動受信翻訳は原文を英語と仮定し、翻訳先が英語の場合は漢字から中国語入力を選択します。言語パックを追加しても任意の原文言語を自動識別できるわけではありません。ローカル処理ではチャットを翻訳提供元へ送信しません。実際の遅延と訳文はモデル、言語ペア、メッセージ、ハードウェア／回線によって異なります。

### Benchmark · AMD Ryzen 7 9700X

| 翻訳方式 | 英→中 / 中→英の中央値 | 最大メモリ | 連続翻訳時のCPU | 有効応答 | 出力の傾向 |
|---|---:|---:|---:|---:|---|
| **Bergamot** | **13 / 19 ms** | **439 MiB** | **3.3%** | 72/72 | 最短の遅延。一部のゲーム略語・用語を誤訳 |
| CTranslate2 | 64 / 90 ms | 450 MiB | 18.1% | 72/72 | 低遅延。ゲーム用語に誤訳あり |
| Qwen3 0.6B | 181 / 99 ms | 1.09 GiB | 49.6% | 69/72 | 訳抜けやプロンプト例のコピーあり |
| Qwen3 1.7B | 479 / 515 ms | 1.89 GiB | 49.6% | 72/72 | 応答は安定しているが、明らかな誤訳あり |

CPUによる英語 ↔ 簡体字中国語の翻訳エンジン測定です。CPUは連続翻訳時の負荷で、待機時の使用率ではありません。有効応答数は翻訳精度を表しません。画面取得、OCR、UI描画、用語辞書による即時変換は含みません。

### API の初期設定例

通常のカスタム接続として設定します。UI 言語や翻訳先言語は変わりません。DeepSeek Flash は非思考モードに対応し、Google は Gemini 3.5 Flash-Lite の用途に翻訳を挙げています。公式資料：[DeepSeek](https://api-docs.deepseek.com/)、[Gemini モデル](https://aistudio.google.com/docs/models/gemini-3.5-flash-lite)、[Gemini の用途・料金](https://aistudio.google.com/docs/pricing#gemini-3.5-flash-lite)。

| 設定 | DeepSeek | Google Gemini |
|---|---|---|
| ベース URL | `https://api.deepseek.com` | `https://generativelanguage.googleapis.com/v1beta/openai` |
| モデル | `deepseek-flash` | `gemini-3.5-flash-lite` |
| API キー | [DeepSeek Platform](https://platform.deepseek.com/) | [Google AI Studio](https://aistudio.google.com/apikey) |
| 追加パラメーター | `{"thinking":{"type":"disabled"}}` | `{}` (モデルの既定値) |

**API & usage → Add** で追加し、形式を **OpenAI Chat Completions**（`openai_chat`）、認証を **Auto**、**Full URL** をオフにします。自分の API キーを入力し、詳細設定で表の追加パラメーターを設定します。カスタムヘッダーは空欄、残高照会は無効のまま保存して接続をテストします。[接続設定](docs/PROVIDERS.md)と Google の [OpenAI 互換 API](https://aistudio.google.com/docs/openai)を参照してください。

モデルの提供範囲、料金、利用枠、データの扱いは提供元が定めます：[DeepSeek 料金](https://api-docs.deepseek.com/quick_start/pricing/)、[Gemini 料金・データ利用](https://aistudio.google.com/docs/pricing)、[Gemini 提供地域](https://aistudio.google.com/docs/available-regions)。Gemini の無料枠と有料枠では制限とデータ利用条件が異なります。

## 仕組み

1. **BianLingo アドオン**がゲームのアドオン API からチャットを読み、ゲーム画面の隅にエンコードしたピクセルを短時間表示します。
2. **デスクトップアプリ**がピクセルを読み取り、選択した接続で翻訳してオーバーレイへ表示します。

ゲームのメモリ読み取り、コード注入、チャットの自動送信は行いません。OCR は自分で指定した領域を取り込みます。Blizzard がアドオンに本文を公開しない場合、ピクセル転送からその本文を復元することはできません。OCR は別の入力経路として利用できます。

## 動作要件と言語

| プラットフォーム | 状況 |
|---|---|
| Windows 10/11 x64 | 利用可能。ウィンドウ OCR には Windows 10 1903 以降が必要です。 |
| macOS | 開発中です。今後のリリースで対応予定ですが、macOS ビルドはまだ提供していません。 |

- BianLingo アドオンを有効にした World of Warcraft。ウィンドウまたはボーダーレス表示を使用します。
- ローカルモデル、または自分で用意した API 接続。完全版パッケージには Bergamot の英語 ↔ 簡体字中国語モデルが含まれ、ほかの対応言語ペアは個別に準備できます。

UI は英語、簡体字中国語、日本語、韓国語、フランス語、ドイツ語、スペイン語、ブラジルポルトガル語、ロシア語に対応しています。**チャットの外観 → 表示言語**で選択し、保存するとすぐに反映されます。既定は**システムに合わせる**で、未対応のシステム言語は英語に、中国語ロケールは簡体字中国語になります。UI、README、翻訳先の言語はそれぞれ独立しています。翻訳対応言語はエンジンとモデルによって異なります。 エラーと動作通知は、中国語 UI では中国語、それ以外の UI では英語で表示されます。

アドオンは Classic Era と対応する Classic／Retail クライアント向けの情報を宣言しています。取得可能なチャットはクライアントや場面によって異なります。HDR、複数画面の DPI 差、各ゲームバージョンは環境ごとの確認が必要です。

## クイックスタート

1. **アドオンを配置。** パッケージの `AddOns/BianLingo` を `World of Warcraft/<client>/Interface/AddOns/` にコピーします。`<client>` は `_retail_`、`_classic_era_`、`_forever_` などです。`Interface/AddOns/BianLingo/BianLingo.toc` を確認し、ゲームで有効にして `/reload` を実行します。
2. **起動と移動。** `BianLingo.exe` と `data` を同じ場所に置き、EXE を起動します。**右上の猫アイコンをクリックすると設定が開き、ドラッグするとウィンドウを移動できます。**
3. **翻訳方式を選択。** API 設定で接続を選びます。**Bergamot / CTranslate2:** 翻訳先言語を選び、現在の言語パックを準備するか、言語パック管理から事前にダウンロードします。**Ollama:** モデルを選んでダウンロードします。API キーは不要です。**オンライン API:** 接続を選択・追加し、Base URL、モデル名、API キーを入力して保存・接続テストします。初期設定例は上の表を参照してください。
4. **受信翻訳を設定。** API 設定でチャットの翻訳先言語を指定し、翻訳範囲でチャンネルを選んで保存します。ローカル翻訳には対応する言語ペアの準備が必要です。
5. **返信を翻訳。** 標準は **· / バッククォート（&#96;）キー（通常 Esc の下）**です。入力後 **Enter** で翻訳・コピーし、ゲームには手動で貼り付けます。出力翻訳の設定で言語・スタイルを選択できます。ショートカット欄をクリックし、新しいキーの組み合わせを押して保存すると変更できます。出力と受信の翻訳先は別々です。
6. **ゲームのチャット配置を同期。** WoW 側でウィンドウとチャンネルを設定し、EXE を起動したまま `/bl windows` を実行するか、`/bl settings` のチャットウィンドウ同期を選択します。受信完了後、タブは自動で**置き換わり**ます。追加のインポート操作は不要です。同期時はアドオンを有効にしてください。


同じパッケージのアプリとアドオンを組み合わせて使用してください。中国語以外へ中国語チャットを翻訳する場合、ゲーム内の中国語フィルターが有効なら `/bl cn off` で解除します。

### Windows SmartScreen

この署名のないリリースでは、Windowsが「WindowsによってPCが保護されました」と表示する場合があります。Microsoft Defender SmartScreenによる署名・ダウンロード評価の確認です。新しいファイルや未署名ファイルは未認識の場合があり、署名しても警告が直ちに消える保証はありません。Microsoft公式説明を参照してください。 [Microsoft Learn](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation)

## ドキュメント

[接続設定](docs/PROVIDERS.md) · [ローカルモデル](docs/LOCAL_TRANSLATION.md) · [チャットタブ](docs/CHAT_TABS.md) · [操作・ブロック](docs/MESSAGE_ACTIONS_AND_BLOCKLIST.md) · [スキン](docs/SKINS.md) · [OCR](docs/OCR_FALLBACK.md) · [ビルド](docs/BUILDING.md)

詳細ガイドとプロジェクト文書の既定言語は英語です。各言語の README はページ上部から選べます。

## プライバシーとライセンス

ローカル翻訳は自分の PC で処理します。オンライン接続では指定した提供元へ原文を送信し、その規約と料金が適用されます。ログにはプレイヤー名やプライベートメッセージが含まれる場合があります。[プライバシー](PRIVACY.md)を参照してください。

プロジェクトのコードは [MIT](LICENSE) ライセンスです。依存関係、モデル、用語はそれぞれの規約と帰属表示を維持します。[第三者に関する表示](THIRD_PARTY_NOTICES.md)を参照してください。World of Warcraft と Blizzard Entertainment の商標は各権利者に帰属します。

公開ダウンロードと機能はすべて寄付なしで利用できます。プロジェクト、プライバシー、ライセンスに関するお問い合わせ先：[@erensm1th](https://github.com/erensm1th)。
