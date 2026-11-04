# Local Ollama translation

Ollama translates text locally using an LLM. The same pixel bridge or OCR supplies text; capture, connection selection, terminology/link protection, caching and display remain separate.

## Setup

1. In **Settings → API & usage**, select **Local Ollama**. Selecting this connection takes effect immediately, but does not install a runtime or download a model.
2. Choose an **Ollama model**. New installations default to **`qwen3:0.6b` (about 523 MB)**. Other choices include `qwen3:1.7b` (about 1.4 GB), `qwen3:4b` (about 2.5 GB) and a custom Ollama tag. Sizes describe model downloads, excluding the runtime, not RAM/VRAM usage; upstream tags can change. See the [publisher's model library](https://ollama.com/library/qwen3).
3. Select **Download selected model / Prepare portable Ollama**. The progress window downloads a pinned official ZIP, checks SHA-256 and extracts it into `data/ollama` next to the EXE. Only the selected model is downloaded into `UserData/ollama/models`. No system installer or global Ollama installation is used. Existing models are retained. A successful preparation saves the selected model; if Ollama remains active, the app tests the connection. Failure leaves the earlier configuration usable, and completion does not force a switch back from another engine.
4. For an installed model, **Apply model** saves it without downloading. **Save settings** also saves the current selection. Changing a dropdown/custom tag alone is a draft, not a download or translation request. Applying a local model does not commit unrelated API drafts or delete other models.
5. See [Portable storage](PORTABLE_STORAGE.md) for runtime, model, temporary and diagnostic locations.
6. Automatic chat, manual output and OCR use the active connection. To return to an online API, choose it and save.

The local connection uses `http://127.0.0.1:11435/api/chat` and does not send saved cloud API keys. LAN/public HTTP addresses are not accepted, and failures do not silently fall back to a cloud service. The selected model must be downloaded and the local runtime available. Models unload after about five idle minutes, so the next request can take longer. Thinking output is disabled; long messages have a 1024-token generation limit, while short responses can stop earlier.

No API key is required. Ollama and model files are separate optional downloads, not embedded in the EXE or application ZIP. Each selected model retains its own terms.

## Downloads and notices

The portable runtime is pinned to v0.40.13 with a fixed SHA-256. It is downloaded directly from the publisher, not bundled or mirrored in project archives. The full 75-file ZIP has been inventoried and hash-checked in hosted Windows. Installation preserves the four original component notices and adds pinned Ollama MIT, LLVM/winpthreads notices and links to NVIDIA/Microsoft terms; these components are not collectively relicensed as MIT.

Explicit model preparation records the runtime-reported model digest and license in `UserData/ollama/model-notices`. A custom model without declared terms is recorded as unknown, not assumed free for commercial use. See [Release compliance](RELEASE_COMPLIANCE.md).

## Validation and performance

The pinned runtime and Qwen3 0.6B have passed hosted Windows CPU startup, download and synthetic game-chat inference checks. Model digests and returned terms are recorded in `resources/model-evidence/ollama/cpu-smoke.json`. This does not establish a user's GPU compatibility, gameplay frame-rate cost or translation quality.

Offline setup tests exercise the real PowerShell setup script using simulated CLI/HTTP services without installing software or downloading models. I haven't benchmarked the three Qwen3 choices on the Ryzen 9700X / RX 9070 XT development machine. The [performance comparison](PERFORMANCE_BENCHMARKS.md) lists Ollama as unmeasured there.

A separate RTX 4090 measurement with `gemma4:12b-it-q4_K_M` observed approximately 567 ms English-to-Simplified-Chinese, 299 ms in reverse and 4.5 seconds for a cold request. Those two samples do not represent the smaller default model. Uncontrolled in-game frame-rate snapshots cannot establish a frame-rate loss; reported memory included roughly 13,580 MiB total GPU use and a 1.94 GiB Ollama working set. Game slang produced inconsistent translations and needs further evaluation.

[Bergamot](BERGAMOT_TRANSLATION.md) and [CTranslate2](CTRANSLATE2_TRANSLATION.md) offer dedicated local translation alternatives. Smaller downloads do not by themselves establish better quality or lower gameplay impact. Existing API selections are not automatically replaced.
