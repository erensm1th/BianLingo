# Release evidence and distribution controls

I use the checks below to prepare BianLingo for distribution. The MIT license
covers the project's original code; third-party software, models and terminology
retain their own terms. Passing these checks does not establish legal clearance
or Blizzard approval.

## Release scope

I'm preparing a Windows x64 public beta while keeping development private.
Build workflows produce artifacts without publishing releases or changing
repository visibility. macOS support is in development with no release build yet.
Field-validation limits and terminology review status are in `release-review.json`.

- I've deferred source review of 729 terminology mappings for this release.
  Their exact count/hash, existing notices and unresolved provenance remain
  recorded. Their rights are not cleared by that deferral. Changes to this
  set require a fresh review or recorded deferral.
- CT2 model evidence comprises 48 fixed publisher cards. Upstream licenses and
  conversion claims are preserved together; CC-BY-4.0 governs the upstream
  en-de, ru-en and zh-en models. The original conversion input checkpoint is
  not recorded. Review covers publisher-declared provenance and licenses,
  without establishing rights to all training data.
- CT2 now uses a pinned-source native CPU worker. It excludes Python, Intel,
  MKL, OpenMP and NVIDIA runtimes, uses static CRT, and is independently checked
  for only allowed Windows DLL imports. Full nested notices and build inputs
  accompany it. Binary/source/notice mismatches fail packaging.
- Optional Ollama is downloaded directly from its publisher on explicit action,
  not bundled or mirrored by this project. The complete fixed ZIP was verified
  twice in hosted Windows and all 75 file hashes recorded. Four supplied notices
  remain intact; supplemental LLVM/winpthreads notices and NVIDIA/Microsoft term
  links identify separate terms. Actual CPU Qwen3 0.6B startup, model download,
  inference and model terms were verified. This does not certify the publisher's
  redistribution authority or the user's GPU/driver performance. Anyone separately
  redistributing that external runtime must satisfy its vendor terms.

## Checks and build inputs

`tools/compliance.py` checks fixed notice hashes, exact terminology evidence and
claimed mappings, model cards/attribution, native source/notices and complete
optional-runtime inventory/smoke evidence. `--vcpkg <prefix>` also compares actual
installed notices and dependency versions; use `--json-prefix` if JSON resolves
from a separate prefix. A fixed filename without matching evidence is insufficient.

The package includes Bergamot's matching `data/bergamot/corresponding-source.zip`, source manifest and source-access instructions. The archive contains actual patches, dependencies, PCRE2, wrapper/build instructions and the generated Marian header.

The Windows packager verifies the exact staging layout, rejects user data and symlinks, checks source/helper/model hashes, native imports and notices, then creates an inventory. The ZIP has `BianLingo.exe`, `data` and `AddOns` at its top level. Policies and review records are in `data/Notices`; `COMPONENT_LICENSES.txt` includes the full component license texts. The package verifier checks component hashes and isolated startup.

```powershell
python tools/compliance.py --vcpkg vcpkg_installed/x64-windows-static
cmake --install build --config Release --prefix dist/windows-x64
python tools/package_windows.py --root dist/windows-x64 --output dist/BianLingo-preview.zip
```

Formal Windows candidate packaging additionally requires `--formal`, approved
scoped review, no pending items, exact deferred-set evidence and the reviewed
eligible-source fingerprint. Clearing the pending list does not bypass these
checks. CI formal candidates require an exact `vX.Y.Z` tag matching CMake and
produce artifacts only. Never add an uploader bypassing these checks.

## Public source snapshot

After review and commit, `python tools/export_public_snapshot.py --output <new.zip>`
exports only allowlisted source, notices and evidence from a clean tree. Personal data, development notes and caches are
excluded. Text line endings are normalized without altering byte-pinned vendor
evidence. A separate archive inspection checks every entry/hash and known secret
patterns. The source approval fingerprint excludes only its own review record;
eligible source changes invalidate it. Secret-pattern scans are limited, so
review the archive contents before publication. I keep development in the private
repository and prepare public source from this reviewed snapshot.

`RELEASE_READINESS.md` records engineering validation and the remaining unsigned
Windows, HDR/OCR and actual-game field limits. I am developing macOS support
separately; the initial release does not include Mac testing or binaries.
