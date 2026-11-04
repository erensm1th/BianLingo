# Translation connections

The **API & usage** list contains editable connections. DeepSeek and Gemini are provided as starting configurations; they do not restrict which providers you can use. Up to 32 API connections are supported, without a domain or model allowlist.

Choose **Gemini**, enter your own Google AI Studio API key, then save and test. Its defaults are `https://generativelanguage.googleapis.com/v1beta/openai`, model `gemini-3.5-flash-lite`, OpenAI Chat Completions and automatic Bearer authentication, matching Google's [OpenAI-compatible endpoint](https://aistudio.google.com/docs/openai). Extra parameters and headers are empty; balance lookup is disabled. The address and model remain editable.

The Gemini starter is added once to existing configurations that have no official Google Gemini connection and have space in the list. Existing keys, custom settings and the active connection are preserved. Save settings to persist it; a saved deletion is respected on later starts.

## Simple setup

1. Open **API & usage** and choose **Add**.
2. Enter a connection name, HTTPS API base URL, API key and model name.
3. Select **Save settings** at the bottom of the main window. Selection, renaming, deletion and advanced edits remain drafts until saved.
4. Use **Send test request** to check the saved connection. This sends a real, short model request that the provider may charge for. Its usage belongs to that connection; it does not enter the chat journal.

OpenAI Chat Completions is the default format. Compatible services generally need no advanced setup or vendor preset; model names are editable. New connections have empty extra parameters and do not force `thinking`, `temperature` or `max_tokens`. Switching a connection's endpoint/protocol clears untouched service-specific defaults while retaining explicitly edited advanced settings.

See the [README examples](../README.md#api-starting-configurations) for starting configurations.

## Advanced settings

| Request format | Path appended to base URL | Automatic authentication |
|---|---|---|
| OpenAI Chat Completions | `/chat/completions` | `Authorization: Bearer` |
| OpenAI Responses | `/responses` | `Authorization: Bearer` |
| Claude Messages | `/messages` | `x-api-key`, plus `anthropic-version` |
| Native Gemini | `/models/<model>:generateContent` | `x-goog-api-key` |

Include the provider's version/routing prefix in the base URL, such as `https://api.example.com/v1`. Native Gemini commonly uses `/v1beta`; native Claude commonly uses `/v1`. Configure the actual protocol in use, including when a vendor supplies a compatibility endpoint.

**Full request URL** uses the URL as entered and appends no path or version. In Gemini base-URL mode, the model is safely encoded as a path segment and an optional `models/` prefix is removed.

Authentication can be set to Bearer, `x-api-key`, `x-goog-api-key`, or no built-in authentication when supplied by custom headers. Headers support `${API_KEY}`:

```json
{"X-Vendor-Key":"${API_KEY}","X-Project":"your-project"}
```

Custom headers override defaults case-insensitively. Transport-managed headers such as `Host` and `Content-Length`, and values containing newlines, are forbidden.

Extra parameters are a JSON object:

```json
{"temperature":0.3,"max_completion_tokens":1024}
```

Parameters use JSON Merge Patch; `null` removes a default. Options differ between models and protocols. Native Claude supplies the required `max_tokens=1024` unless changed. Parameters cannot replace the model, input text or prompts, or force streaming; the app manages these fields.

### Optional balance API

Balance lookup is disabled by default:

```json
{"type":"none"}
```

For a provider with a balance GET endpoint, specify its full URL, amount field and currency:

```json
{"type":"custom","url":"https://api.example.com/balance","amount_path":"/data/balance","currency":"USD"}
```

Paths support JSON Pointer or dotted notation such as `data.balance`. Balance requests use the connection's authentication and headers. Retrieving a balance does not verify model access; use the model test separately. A connection without a balance endpoint displays that state, makes no unrelated provider balance request and does not present token usage as money.

## Saving and switching

- Keys, URLs, models, headers, parameters and balance settings are encrypted together in `settings.json` as `connection_store`, expanded only in memory. No separate plaintext connection file is written.
- The active connection ID and encrypted store are committed in one atomic replacement. Validation or file replacement failure leaves the active connection unchanged.
- A blank API-key field retains the saved key for that connection. New keys in separate connection drafts remain separate.
- Changing a connection cancels queued work, discards stale interface results and isolates translation caches. Already-issued HTTP requests may still finish and incur costs; returned usage is credited to the original connection.
- Token usage is counted per connection with a global history summary. Balance requests do not count as model requests; test requests do.
- Credential protection is local to the platform/user; copying encrypted settings to another computer is not guaranteed to make them readable. The application needs no Python runtime.

Compatible HTTPS services must use one of the four supported request/response formats. Private protocols, dynamic OAuth or cloud-signature authentication need an adapter; the app does not guess arbitrary HTTP formats, probe endpoints automatically or retry billable requests automatically. Offline contract tests cover request bodies, response parsing, token counts, authentication, URLs, encrypted saves, failed saves and UI interaction. They do not establish live compatibility with every provider/model. Test your own endpoint and key.

Protocol references: [OpenAI Chat Completions](https://developers.openai.com/api/reference/resources/chat/subresources/completions/methods/create), [OpenAI Responses](https://developers.openai.com/api/docs/guides/migrate-to-responses), [Claude Messages](https://platform.claude.com/docs/en/api/messages), [Gemini generateContent](https://ai.google.dev/api/generate-content).

## Prompts and language selection

API connections and Ollama share the incoming prompt and separate short/standard outgoing prompts. Translation targets are independent of the interface language. `{language}` expands to the selected target; incoming custom prompts without it remain supported by a final target-language instruction. Built-in presets follow the nine supported interface languages, with English fallback for unsupported system locales.

Short English output permits familiar player abbreviations and casual lowercase prose while preserving player-name case. Other languages follow their own writing conventions, including required German noun capitalization. Standard output keeps ordinary spelling, capitalization and punctuation. Matching terminology is a context reference expressed in the target language, not an instruction to copy the reference language. At most 12 distinct references / 2 KiB are appended per request; the entire dictionary is not sent.

Bergamot and CTranslate2 do not consume prompts. The settings page states this and retains edits for a later switch to API/Ollama. Exact English outgoing phrases can return directly from the local dictionary. The bundled Simplified Chinese incoming dictionary is a direct-output fast path only for Simplified Chinese; other targets use the selected engine. Link placeholders are protected and restored in both directions.

Only exact recognized built-in preset text is refreshed when defaults or interface language change. User-edited prompts and prefixes remain unchanged. Incoming and outgoing editors have separate reset controls. Prompt edits require saving settings; the output window's style selector applies to subsequent submissions. Each LLM request contains one system instruction and one user message, without conversation history. Extra parameters cannot replace those fields.

Filtering is conservative, not general language identification. For an English target, API/Ollama still receives meaningful all-ASCII prose: English, French and other Latin languages cannot reliably be distinguished by script alone. A prompt asks the model to leave target-language content unchanged; such requests can still consume tokens. Dedicated workers keep their known-direction filter. Non-Chinese targets accept Chinese prose. The addon independently filters Chinese before transmission: use `/bl cn off` when that source is needed for another target. Output quality and instruction following depend on the model; small local LLMs may be less reliable.
