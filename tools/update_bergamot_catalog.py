"""Update the data-driven catalogue from Mozilla's stable direct models.

Keeps the existing pinned/bundled directions so an update does not invalidate
installed models. This downloads catalogue metadata only, never model weights.
"""
import argparse
import json
from pathlib import Path
import re
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
URL = "https://firefox.settings.services.mozilla.com/v1/buckets/main/collections/translations-models/records"
CDN = "https://firefox-settings-attachments.cdn.mozilla.net/"


def catalogue(records, pinned):
    groups = {}
    for record in records:
        version = record.get("version", "")
        if not re.fullmatch(r"\d+(?:\.\d+)*", version):
            continue  # Alpha models are not offered as stable downloads.
        source, target, role = record.get("fromLang"), record.get("toLang"), record.get("fileType")
        if not source or not target or role not in ("model", "lex", "vocab", "srcvocab", "trgvocab"):
            continue
        group = groups.setdefault((source, target), {})
        priority = (tuple(map(int, version.split("."))), record.get("last_modified", 0))
        if role not in group or priority > group[role][0]:
            group[role] = priority, record
    result, pinned_pairs = [], {r["pair"] for r in pinned}
    for original in pinned:
        r = dict(original)
        source, target = (r["pair"][:-3], "en") if r["pair"].endswith("-en") else ("en", r["pair"][3:])
        r.update(source=source, target=target, role=r["name"].split(".", 1)[0])
        result.append(r)
    for (source, target), group in sorted(groups.items()):
        pair = source + "-" + target
        if pair in pinned_pairs or not all(k in group for k in ("model", "lex")):
            continue
        vocab = ["vocab"] if "vocab" in group else ["srcvocab", "trgvocab"]
        if not all(k in group for k in vocab):
            continue
        selected = [group[k][1] for k in ("model", "lex", *vocab)]
        if not group["model"][1]["name"].endswith(".intgemm.alphas.bin"):
            continue
        for r in selected:
            attachment = r["attachment"]
            result.append(dict(pair=pair, source=source, target=target, role=r["fileType"],
                               name=r["name"], url=CDN + attachment["location"],
                               sha256=attachment["hash"], size=attachment["size"], version=r["version"]))
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--records", type=Path, help="Existing official records JSON (otherwise fetch metadata)")
    parser.add_argument("--output", type=Path, default=ROOT / "resources/bergamot-models.json")
    args = parser.parse_args()
    old = json.loads(args.output.read_text(encoding="utf8"))
    # Baseline directions have received actual engine checks. Other catalogue
    # entries can be refreshed by rerunning this tool without a UI code change.
    pinned = [r for r in old["models"] if r.get("pinned", True)]
    raw = args.records.read_bytes() if args.records else urlopen(URL, timeout=30).read()
    models = catalogue(json.loads(raw)["data"], pinned)
    for r in models:
        r["pinned"] = r["pair"] in {x["pair"] for x in pinned}
    for model in models:
        model.update(license="MPL-2.0", publisher="Mozilla / Firefox Translations",
                     license_source="https://github.com/mozilla/translations",
                     changes="Original model data; bundled files may be losslessly compressed with zstd.")
    old.update(models=models, catalogue_source=URL, catalogue_format="stable-intgemm", schemaVersion=2)
    args.output.write_text(json.dumps(old, ensure_ascii=False, indent=2) + "\n", encoding="utf8")
    print(f"Catalogued {len({r['pair'] for r in models})} direct pairs, {len(models)} files; no weights downloaded")


if __name__ == "__main__":
    main()
