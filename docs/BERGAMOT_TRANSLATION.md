# Local Bergamot translation

Select **Local Bergamot** under **Settings → API & usage**. No API key is needed. `resources/bergamot-models.json` contains a snapshot of Mozilla's stable intgemm catalog: **58 languages, 114 direct directions and 346 files**, excluding alpha models. English ↔ Simplified Chinese models are included in the full package; other directions are downloaded on demand.

## Prepare before playing

**Prepare current language** lists the models required for the selected chat target and starts preparation. **Language packs** opens the manager without downloading. The reverse direction is included by default and can be deselected. An English target follows the existing Chinese-source route; Portuguese regional choices use the generic Portuguese model. Target changes update the planned queue, not an already-running job.

Search by language name or code, such as `ja` or `en-ja`, select an available direct direction and add it to the download list. Languages use their native names and codes. The deduplicated queue supports removing selected entries, clearing, per-direction status/size and **Prepare download list**. Status distinguishes missing, partial, bundled-but-compressed and installed files. Presence/size checks provide a quick display; preparation and first use verify pinned SHA-256. Preparing an installed pair verifies or repairs it.

Downloads live in `UserData/bergamot/models/<pair>` next to the EXE. Bundled compressed files and the worker live in `data/bergamot`. A missing model prompts you to prepare it; chat processing does not start a hidden download or switch to an API. The downloader requests only catalogued Mozilla HTTPS locations and sends no chat text. See [Portable storage](PORTABLE_STORAGE.md).

A background job captures its queue without committing API drafts or changing the active connection/target. Closing the manager leaves it running. Cancellation retains completed files and removes the current temporary file; it may wait for a network operation to finish. Retrying verifies/skips completed files and restarts incomplete files without byte-range resume. Application exit cancels and joins the worker. Preparing another pair does not block already-installed directions.

## Directions and English pivot

Stable models mostly connect a language with English. For a known source and target without a direct model, the planner can use `source → English → target`; both legs must be ready. This supports known directions such as Chinese-to-Japanese or French-to-Chinese. Two inference steps can cost more time and compound errors.

**Automatic incoming chat still assumes English input, or uses Han-script detection for Chinese input when the target is English. It does not identify arbitrary source languages.** There are 26 incoming target choices and ten outgoing choices. Other catalogued pairs can be downloaded for preparation; availability does not add universal source identification. Manual output can use a known source/target pivot. CTranslate2 has its own catalog and preparation flow; Ollama uses multilingual models rather than per-language packs.

## Catalog and worker

The catalog comes from [Mozilla Remote Settings](https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/translations-models/records). Entries record source/target, role, version, size, URL and SHA-256. `tools/update_bergamot_catalog.py` refreshes metadata without fetching weights, preserving pinned verified/bundled files and adding complete stable directions. The shipped catalog is a snapshot, not periodically refreshed online.

The project builds its worker from pinned [Bergamot source](https://github.com/browsermt/bergamot-translator). Preparation writes `files.list`; the worker loads verified model, vocabulary and shortlist roles for the requested pair. Build with `tools/build_bergamot.py` and `tools/prepare_bergamot.py`, then set CMake's `BL_BERGAMOT_RUNTIME` to the output directory. Required notices remain in [Third-party notices](../THIRD_PARTY_NOTICES.md) and `data/bergamot/engine-LICENSES.txt`.

## Validation limits

Real-model checks cover Japanese, Traditional Chinese and Chinese → English → Japanese, as well as English ↔ Simplified Chinese, French, German and Spanish. Other catalogued directions have not each been tested, and catalog inclusion is not a guarantee of game terminology or language quality. Abbreviations, fragments and protected links need real-chat evaluation. Bergamot does not interpret LLM prompts. The worker runs at low priority and exits after ten idle minutes; there is no controlled same-scene gameplay frame-rate comparison.
