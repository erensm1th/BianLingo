"""Real model smoke test for the project-owned Bergamot pipe helper."""

import argparse
import hashlib
import json
import subprocess
import struct
from pathlib import Path
from tempfile import TemporaryDirectory


def read_exact(stream, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = stream.read(size - len(data))
        if not chunk:
            raise AssertionError("Bergamot helper closed the pipe")
        data.extend(chunk)
    return bytes(data)


def translate(process, direction: int, directory: Path, text: str, html: bool = False) -> str:
    path = str(directory).encode("utf-8")
    body = text.encode("utf-8")
    process.stdin.write(struct.pack("<BBII", direction, int(html), len(path), len(body)) + path + body)
    process.stdin.flush()
    status, length = struct.unpack("<BI", read_exact(process.stdout, 5))
    assert length <= 120000
    result = read_exact(process.stdout, length).decode("utf-8")
    assert status == 0, result
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--helper", type=Path, required=True)
    models = parser.add_mutually_exclusive_group(required=True)
    models.add_argument("--model-root", type=Path)
    models.add_argument("--runtime", type=Path)
    args = parser.parse_args()
    temporary = TemporaryDirectory() if args.runtime else None
    model_root = args.model_root if temporary is None else Path(temporary.name)
    if args.runtime:
        import zstandard

        manifest = json.loads((args.runtime / "models.json").read_text(encoding="utf-8"))
        for record in manifest["models"]:
            if "compressed_sha256" not in record:
                continue
            compressed = (args.runtime / "compressed" / record["pair"] /
                          (record["name"] + ".zst")).read_bytes()
            assert hashlib.sha256(compressed).hexdigest() == record["compressed_sha256"]
            target = model_root / record["pair"] / record["name"]
            target.parent.mkdir(parents=True, exist_ok=True)
            plain = zstandard.ZstdDecompressor().decompress(compressed, max_output_size=record["size"])
            assert hashlib.sha256(plain).hexdigest() == record["sha256"]
            target.write_bytes(plain)
    with subprocess.Popen([str(args.helper)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE) as process:
        try:
            assert read_exact(process.stdout, 6) == b"BLMT1\n"
            chinese = translate(process, 0, model_root / "en-zh-Hans", "Hello, friend.")
            assert any("\u4e00" <= ch <= "\u9fff" for ch in chinese), chinese
            english = translate(process, 1, model_root / "zh-Hans-en", "你好，朋友。")
            assert any("a" <= ch.lower() <= "z" for ch in english), english
            protected = translate(process, 0, model_root / "en-zh-Hans",
                                  "Meet at <code>[[BLG_LINK_1]]</code>.", html=True)
            assert "[[BLG_LINK_1]]" in protected, protected
            # The pipe protocol is UTF-8; CI's redirected console can still be
            # cp1252. Keep diagnostic output lossless and ASCII-safe there.
            print(json.dumps({"en-zh": chinese, "zh-en": english, "HTML": protected}, ensure_ascii=True))
        except Exception:
            code = process.poll()
            if code is None:
                process.terminate()
                code = process.wait(timeout=5)
            print(f"helper exit code: {code:#x}")
            print(process.stderr.read().decode("utf-8", errors="replace")[-4000:])
            raise
    if temporary:
        temporary.cleanup()


if __name__ == "__main__":
    main()
