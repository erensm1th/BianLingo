# Windows release validation

Releases target Windows 10/11 x64; macOS support is under development. The release checks cover C++ and Lua protocol behavior, settings recovery, translation adapters, UI layout in nine languages, source provenance and package contents. Real-model checks exercise Bergamot and CTranslate2; optional Ollama has pinned publisher-runtime evidence.

Record validation results in the release evidence. Host-based checks do not replace Windows 10 hardware, HDR/OCR, mixed-DPI, actual-game performance or in-game list/SavedVariables acceptance. Windows packages require an explicit Authenticode setup to be signed.

The source evidence gate applies to the reviewed source fingerprint. All notices, exact terminology deferral and known limitations remain part of the engineering review. A passed gate does not grant third-party rights or Blizzard approval.
