"""Compare local translation latency and process memory using the same short sentence."""

import argparse
import ctypes
import hashlib
import json
import statistics
import struct
import subprocess
import tempfile
import time
from pathlib import Path

import zstandard


def exact(stream, n):
    data = bytearray()
    while len(data) < n:
        part = stream.read(n - len(data))
        if not part:
            raise RuntimeError("Local engine closed the pipe")
        data.extend(part)
    return bytes(data)


def memory(pid):
    class C(ctypes.Structure):
        _fields_ = [("cb", ctypes.c_ulong), ("faults", ctypes.c_ulong)] + [
            (n, ctypes.c_size_t) for n in ("peak", "working", "q1", "q2", "q3", "q4", "page", "peakpage")]
    h = ctypes.windll.kernel32.OpenProcess(0x0410, False, pid)
    if not h:
        raise OSError("Cannot inspect local engine process")
    c = C()
    c.cb = ctypes.sizeof(C)
    ctypes.windll.psapi.GetProcessMemoryInfo(h, ctypes.byref(c), c.cb)
    ctypes.windll.kernel32.CloseHandle(h)
    return round(c.working / 1024**2, 1)


def send(process, header, body):
    start = time.perf_counter()
    process.stdin.write(header + body)
    process.stdin.flush()
    status, size = struct.unpack("<BI", exact(process.stdout, 5))
    result = exact(process.stdout, size).decode("utf-8")
    if status:
        raise RuntimeError(result)
    return result, round((time.perf_counter() - start) * 1000, 1)


parser = argparse.ArgumentParser()
parser.add_argument("--bergamot-runtime", type=Path, required=True)
parser.add_argument("--ctranslate2-runtime", type=Path, required=True)
parser.add_argument("--scratch", type=Path, required=True)
parser.add_argument("--sentence", default="LFM 2 DPS for SOO 10m, need heals, pst")
args = parser.parse_args()
bergamot = args.bergamot_runtime.resolve()
ct2 = args.ctranslate2_runtime.resolve()
args.scratch.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(dir=args.scratch) as temp:
    models = Path(temp)
    for record in json.loads((bergamot / "models.json").read_text())["models"]:
        if "compressed_sha256" not in record:
            continue  # Additional language directions are not bundled.
        compressed = (bergamot / "compressed" / record["pair"] /
                      (record["name"] + ".zst")).read_bytes()
        if hashlib.sha256(compressed).hexdigest() != record["compressed_sha256"]:
            raise ValueError("Bergamot model hash mismatch")
        target = models / record["pair"] / record["name"]
        target.parent.mkdir(parents=True, exist_ok=True)
        plain = zstandard.ZstdDecompressor().decompress(compressed, max_output_size=record["size"])
        if hashlib.sha256(plain).hexdigest() != record["sha256"]:
            raise ValueError("Bergamot decompressed model hash mismatch")
        target.write_bytes(plain)
    sentence = args.sentence
    for name, command in (
        ("Bergamot", [str(bergamot / "BianLingoBergamot.exe")]),
        ("CTranslate2", [str(ct2 / "python" / "python.exe"), "-u", str(ct2 / "helper.py")]),
    ):
        with subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE) as process:
            greeting = exact(process.stdout, 6)
            if greeting not in ((b"BLMT1\n",) if name == "Bergamot" else (b"BLCT2\n", b"BLCT3\n")):
                raise RuntimeError("Unexpected local engine handshake")
            samples = []
            result = ""
            for i in range(11):
                data = sentence.encode("utf-8")
                if name == "Bergamot":
                    path = str(models / "en-zh-Hans").encode("utf-8")
                    header = struct.pack("<BBII", 0, 0, len(path), len(data))
                    body = path + data
                else:
                    header = struct.pack("<BI", 0, len(data))
                    body = data
                result, elapsed = send(process, header, body)
                samples.append(elapsed)
            print(name, "first_ms", samples[0], "warm_median_ms", statistics.median(samples[1:]),
                  "warm_max_ms", max(samples[1:]), "working_set_mb", memory(process.pid),
                  "result", result.encode("unicode_escape").decode("ascii"))
