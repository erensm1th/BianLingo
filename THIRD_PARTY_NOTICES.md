# Third-party notices

This project is licensed under the MIT license in `LICENSE`. The Windows package also contains the components below. Their licenses remain their own; this project's MIT license does not replace them.

I maintain BianLingo as a free, non-commercial fan project. That choice does not restrict the MIT license or waive third-party terms. See `PROJECT_NOTICE.md` for trademarks, game interaction, warranty limitations and voluntary support. Upstream licenses remain in effect.

| Component | Source | License and included notice |
| --- | --- | --- |
| Bergamot translator and libraries in `BianLingoBergamot.exe` | [Bergamot source at `9271618ebbdc5d21ac4dc4df9e72beb7ce644774`](https://github.com/browsermt/bergamot-translator/tree/9271618ebbdc5d21ac4dc4df9e72beb7ce644774); our wrapper and reproducible build patches are in `tools/bergamot_helper` and `tools/build_bergamot.py` | MPL-2.0 and component specific licenses; source locations and full license texts in `data/bergamot/engine-LICENSES.txt` |
| Firefox translation models (English ↔ Simplified Chinese bundled; other catalogued directions downloaded on demand) | [Mozilla translations](https://github.com/mozilla/translations); pinned files, URLs and SHA-256 in `data/bergamot/models.json` and `resources/bergamot-models.json` | MPL-2.0; [license text](https://www.mozilla.org/en-US/MPL/2.0/) |
| OpenSSL | [openssl.org](https://www.openssl.org/source/) | Apache-2.0; notice embedded in the application's component notice |
| nlohmann/json | [nlohmann/json](https://github.com/nlohmann/json) | MIT; notice embedded in the application's component notice |
| Tesseract and Leptonica (Windows OCR) | [Tesseract](https://github.com/tesseract-ocr/tesseract), [Leptonica](https://github.com/DanBloomberg/leptonica) | Apache-2.0 and BSD-style license respectively; notices embedded in the application's component notice |
| Tesseract OCR language data | [tessdata_fast](https://github.com/tesseract-ocr/tessdata_fast) | Apache-2.0; notice embedded in the application's component notice |
| Terminology data | [WoWTranslator](https://github.com/Pirson-s-Addons/WoW-Translator), bundled LibBabble tables and historical project/user mappings | WoWTranslator MIT (Pirson); separate LibBabble MIT tables by ckknight, nevcairiel, Ackis, arith, dynaletik and translation contributors; LibBabble core declares Public Domain. Exact source headers are embedded as Terminology.txt. Source snapshot, SHA-256 and unresolved mappings: resources/data/glossary-provenance.json |

The app's Settings → Status → component notice provides the remaining bundled component notices. Ollama and its selected model are installed separately by the user and are not included in the Windows ZIP.

The Windows package includes a native CPU-only CTranslate2 4.8.4 / SentencePiece
0.2.2 worker. Fixed source inputs and hashes are in `ct2-native-sources.json`;
full MIT, Apache, BSD and other nested notices are embedded as CT2Native.txt and
installed beside the worker as engine-LICENSES.txt. Ruy, cpu_features, cpuinfo,
spdlog, Abseil, protobuf-lite, bundled math headers, BS thread pool and JSON retain
their own terms. This worker uses static CRT and imports only Windows system DLLs;
it contains no Python, Intel OpenMP, MKL, CUDA or cuDNN runtime. Reference Python
helpers are not shipped or loaded.

CT2 weights are selected/downloaded separately. Each installation retains model
attribution, changes, fixed source/conversion cards and both license texts.
en-de, ru-en and zh-en upstream terms are CC-BY-4.0; the converter's Apache-2.0
claim does not replace those terms. All 48 fixed cards were reviewed for declared
lineage and notices. The original input checkpoint remains unrecorded; no claim
of reproducing it or clearing every training-data right is made. Exact evidence
is in resources/model-evidence/ctranslate2 and the model catalog.

Ollama remains a publisher-direct optional download, never bundled or mirrored
in project archives. The complete official v0.40.13 ZIP was hash-verified and
inventoried in hosted Windows, and CPU Qwen3 0.6B startup/download/inference was
tested. Its four original component notices are retained. OllamaRuntime.txt
adds the pinned Ollama MIT, LLVM and winpthreads notices and identifies NVIDIA
CUDA and Microsoft VC components under their separate vendor terms. This is an
inventory/preservation review, not a blanket MIT grant or an authorization to
redistribute the external GPU/CRT DLLs. The official ZIP has no root LICENSE;
the installer adds the source-pinned Ollama MIT text alongside original notices.
Qwen3 model cards state Apache-2.0; custom model terms remain independent and
actual selected model digests/licenses are saved during preparation.

The main EXE statically links libarchive and the OCR dependency chain. Full exact
notices for bzip2, curl, giflib, libarchive, libjpeg-turbo, liblzma, libpng, libwebp,
lz4, openjpeg, tiff, zlib and zstd are embedded in OCRDependencies.txt. zstd is
used under its BSD-3-Clause option. Build-input version evidence is distinguished
from proven linked code in `artifact-inventory.json`.

The package includes the actual patched Bergamot source archive. See
`SOURCE_ACCESS.md`; access does not require a private repository account. Do not
delete that archive when redistributing the helper. Model source URLs and hashes
are in the accompanying model catalog; Mozilla states its model files are MPL-2.0.

I drew the original two-cat logo for BianLingo.
Application/addon icons are size and format derivatives of that complete image;
see `resources/ASSET_PROVENANCE.md`. No Blizzard artwork or fonts are bundled.
This is an unofficial project with no implied endorsement. See `PRIVACY.md` for
local logs, remote API processing and storage. `RELEASE_REVIEW.json` records
review status, unresolved items and known limitations.

## Trademarks and independent project status

Warcraft, World of Warcraft and Blizzard Entertainment are trademarks or registered
trademarks of Blizzard Entertainment and their respective rights holders. This
independent fan project is not made, reviewed, approved, supported or endorsed by
Blizzard. Model publishers, Mozilla, API providers and component authors do not
endorse the project merely because their names or components are identified here.
Original project assets, third-party components and game assets retain their
respective ownership. See PROJECT_NOTICE.md for the voluntary support policy,
accurate technical behavior and rights/privacy contact route. Upstream licenses determine the rights
granted for each component.
