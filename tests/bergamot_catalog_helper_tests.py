"""Manual real-weight checks for the catalogue helper; never downloads files."""
import argparse
import json
from pathlib import Path
import struct
import subprocess


def translate(helper, directory, text):
    directory = str(directory.resolve()).encode("utf8")
    body = text.encode("utf8")
    request = struct.pack("<BBII", 0, 0, len(directory), len(body)) + directory + body
    result = subprocess.run([str(helper)], input=request, capture_output=True, timeout=45)
    assert result.returncode == 0, result.stderr.decode("utf8", errors="replace")[-800:]
    assert result.stdout.startswith(b"BLMT1\n"), "Missing helper handshake"
    status, size = struct.unpack("<BI", result.stdout[6:11])
    text = result.stdout[11:11 + size].decode("utf8")
    assert status == 0 and text, text
    return text


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--cache", type=Path, required=True)
    parser.add_argument("--bundled-cache", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    helper = (args.runtime / "BianLingoBergamot.exe").resolve()
    japanese = translate(helper, args.cache / "models/en-ja", "Hello, friend.")
    traditional = translate(helper, args.cache / "models/en-zh-Hant", "Please open the door.")
    english = translate(helper, args.bundled_cache / "models/zh-Hans-en", "你好，朋友。")
    pivot = translate(helper, args.cache / "models/en-ja", english)
    assert japanese != "Hello, friend." and traditional != "Please open the door." and english != "你好，朋友。"
    report = dict(japanese=japanese, traditional=traditional, pivot_english=english, pivot_japanese=pivot)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf8")
    print(json.dumps(report, ensure_ascii=False))
    print("PASS real Japanese/Traditional Chinese weights and Chinese -> English -> Japanese pivot")


if __name__ == "__main__":
    main()
