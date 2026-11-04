"""Prepare the pinned Firefox translation models for the own Bergamot helper."""

import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def download(url: str) -> bytes:
    with urlopen(url, timeout=120) as response:
        return response.read()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)

    manifest_data = (ROOT / "resources" / "bergamot-models.json").read_bytes()
    (output / "models.json").write_bytes(manifest_data)
    manifest = json.loads(manifest_data)
    for record in manifest["models"]:
        if record["pair"] not in ("en-zh-Hans", "zh-Hans-en"):
            continue  # Other catalogue pairs are prepared explicitly in the desktop UI.
        folder = output / "compressed" / record["pair"]
        folder.mkdir(parents=True, exist_ok=True)
        target = folder / (record["name"] + ".zst")
        if target.exists() and digest(target.read_bytes()) == record["compressed_sha256"]:
            continue
        compressed = download(record["url"])
        if digest(compressed) != record["compressed_sha256"]:
            raise ValueError(f"Compressed model SHA-256 mismatch: {record['name']}")
        temporary = target.with_suffix(target.suffix + ".tmp")
        temporary.write_bytes(compressed)
        temporary.replace(target)
        print(f"Prepared {record['pair']}/{record['name']}", flush=True)


if __name__ == "__main__":
    main()
