# Local CTranslate2 language packs

Choose **Local CTranslate2** in **Settings → API & usage**. No API key is needed. **Prepare current language** plans dependencies from the target; **Language packs** supports search, reverse directions, removal, clearing, progress and cancellation. Weights are downloaded only after an explicit preparation action, never during chat translation.

The full Windows package includes the approximately 2.6 MB native `BianLingoCT2.exe` worker: CTranslate2 4.8.4, SentencePiece 0.2.2, Ruy CPU INT8, four threads and a static CRT. It needs no Python, pip, separate VC installer, Intel OpenMP, MKL, CUDA or cuDNN.

## Languages and preparation

The pinned catalog provides 24 directions: bidirectional English with Simplified Chinese, French, German, Spanish, Italian, Russian, Arabic, Indonesian and Vietnamese; and Japanese, Korean, Thai, Turkish, Czech and Danish into English. It does not cover every target-menu entry. Simplified Chinese is not substituted for Traditional Chinese. Known sources/targets may pivot through English when both legs are installed.

Jobs retain their selected engine and queue across connection changes and do not save unrelated API drafts. Closing the window leaves preparation running. Cancellation retains completed verified files; application exit cancels and joins the worker. Pinned size and SHA are checked before temporary files are committed. No downloaded installer code is executed. A failed new pair does not damage installed pairs. If the native component is missing or corrupt, extract the full package again; preparation does not install Python as a repair path.

| Contents | Location relative to the EXE |
|---|---|
| Native worker, SHA, build provenance and notices | `data/ctranslate2` |
| Downloaded weights and their attribution/licenses | `UserData/ctranslate2/models/<source-target>` |
| Worker catalog snapshot | `UserData/ctranslate2/models.json` |
| Any explicitly bundled weights | `data/ctranslate2/models/<pair>` |

## Inference boundaries

Bounded BLCT3 pipe requests carry actual direction IDs. At most two models are cached; loading a third releases the oldest. The worker exits after ten idle minutes. Link placeholders and surrounding whitespace are preserved. Truncated or empty results are errors, not successful translations. Callers can cancel pipe waits and terminate their own worker process. Ordinary messages do not repeatedly hash entire weights or add player-information queries.

Automatic chat assumes English input, with the existing Han-script rule for an English target. More catalog entries do not provide arbitrary source-language identification. Players should review terminology, fragments and model errors.

## Sources, notices and builds

`resources/ct2-native-sources.json` pins six source archives and SHA values. `tools/build_ct2_native.py` downloads/builds in the chosen directories using those fixed inputs. Build evidence records the source lock, wrapper hash, notices and actual PE imports. Packaging reparses the PE and permits only Windows system DLLs; notices must match the reviewed `CT2Native.txt`. Set `-DBL_CT2_NATIVE_RUNTIME=<build-output>` for CMake installation. Windows CI builds this worker too.

Model repositories, sizes, hashes, attribution, conversion changes and 48 pinned model cards are retained in the catalog and `resources/model-evidence/ctranslate2`. English-to-German, Russian-to-English and Chinese-to-English retain original CC-BY-4.0 terms alongside the converter's Apache claim; conversion labels do not replace upstream terms. Original conversion-input checkpoints remain unrecorded. Review is based on publisher-declared provenance/licenses, not a claim that inputs were reproduced or all training-data rights cleared. Installation retains attribution and both license layers.

Developers can prepare explicit weights with `tools/prepare_ctranslate2.py --runtime <native-output> --output <empty-directory> --pairs en-zh zh-en`. Catalog tooling produces a review candidate; it does not automatically replace release approvals or the native runtime.
