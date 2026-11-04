"""Windows translation benchmarks; synthetic chat only, no credentials in output.

Requires psutil in the observer environment. Run the native EXCLUDE_FROM_ALL
benchmark target beside data/bergamot and the optional data/ctranslate2 runtime.
"""
from __future__ import annotations

import argparse
import concurrent.futures
from datetime import datetime
import json
import math
import os
from pathlib import Path
import statistics
import struct
import subprocess
import sys
import threading
import time

import psutil


CORPUS = {
    "en-zh": [
        ("short", "im omw, save me a spot"),
        ("short", "wait a sec, healer needs mana"),
        ("short", "can u summon me?"),
        ("short", "great run, thanks everyone!"),
        ("medium", "lf2m dps sm cath, tank and healer ready, pst"),
        ("medium", "wts 12 slot bags, 1g each, your mats or mine"),
        ("medium", "theres a patrol behind us, dont pull the next pack yet"),
        ("medium", "take [[BLG_LINK_1]] x10 for free at the inn"),
        ("long", "we still need two damage dealers and one healer for the dungeon. i can tank, but i need five minutes to repair before we start"),
        ("long", "please interrupt the caster on the left, keep the sheep alive, and wait for the healer to drink before pulling the next group"),
        ("long", "if you need the quest item, stay near the entrance after the last boss. we can help everyone finish the quest before leaving the dungeon"),
        ("long", "selling twelve slot bags and crafted armor in stormwind tonight. bring your own materials if you want free crafting, tips are appreciated"),
    ],
    "zh-en": [
        ("short", "我正在路上，帮我留个位置"),
        ("short", "稍等一下，治疗正在喝水"),
        ("short", "你能把我拉过去吗？"),
        ("short", "这趟打得不错，谢谢大家"),
        ("medium", "血色教堂还缺两个输出，坦克和治疗已经准备好了，私聊我"),
        ("medium", "出售十二格背包，每个一金币，可以自带材料代做"),
        ("medium", "我们后面有一队巡逻怪，先别拉前面的下一波"),
        ("medium", "旅店里可以免费领取[[BLG_LINK_1]]，每个人十个"),
        ("long", "这个副本还需要两个输出和一个治疗。我可以当坦克，不过出发之前要先花五分钟修理一下装备。"),
        ("long", "请打断左边那个施法怪，不要碰被羊的目标，等治疗喝完水再拉下一组怪，我们可以稳一点打。"),
        ("long", "如果你还需要任务物品，打完最后一个首领后请留在入口附近，我们可以帮助所有人完成任务再离开副本。"),
        ("long", "今晚在暴风城出售十二格背包和制造的护甲，自带材料可以免费代做，愿意给小费的话也非常感谢。"),
    ],
}


def percentile(values, p):
    values = sorted(values)
    return values[max(0, math.ceil(len(values) * p) - 1)]


class Monitor:
    """20 ms process-tree sampling. CPU counters are cumulative, not snapshots."""
    def __init__(self, pid, extra_roots=()):
        self.roots = [pid, *extra_roots]
        self.known = {}
        self.last_cpu = {}
        self.current_rss = self.current_private = self.peak_rss = 0
        self.lock = threading.RLock()
        self.stop = threading.Event()
        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()

    def collect(self):
        with self.lock:
            for pid in self.roots:
                try:
                    parent = psutil.Process(pid)
                    for process in [parent, *parent.children(recursive=True)]:
                        self.known.setdefault((process.pid, process.create_time()), process)
                except (psutil.Error, OSError):
                    pass
            rss = private = 0
            for key, process in self.known.items():
                try:
                    if process.create_time() != key[1]:
                        continue
                    cpu = process.cpu_times()
                    self.last_cpu[key] = cpu.user + cpu.system
                    mem = process.memory_info()
                    rss += mem.rss
                    private += getattr(mem, "private", mem.vms)
                except (psutil.Error, OSError):
                    pass
            self.current_rss, self.current_private = rss, private
            self.peak_rss = max(self.peak_rss, rss)
            return {"cpu_ms": sum(self.last_cpu.values()) * 1000,
                    "rss_mib": rss / 2**20, "private_mib": private / 2**20,
                    "sampled_peak_rss_mib": self.peak_rss / 2**20,
                    "processes": len(self.known)}

    def loop(self):
        while not self.stop.wait(.02):
            self.collect()

    def close(self):
        self.stop.set()
        self.thread.join(timeout=2)


class Driver:
    def __init__(self, command, backend, raw=False, model_root=None):
        self.backend, self.raw, self.model_root = backend, raw, model_root
        self.start = time.perf_counter()
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.DEVNULL,
                                        env=dict(os.environ, BIANLINGO_UI_LANGUAGE="zh"),
                                        creationflags=subprocess.CREATE_NO_WINDOW |
                                        (subprocess.BELOW_NORMAL_PRIORITY_CLASS if raw else 0))
        self.monitor = Monitor(self.process.pid)
        self.reader = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        try:
            if raw:
                expected = (b"BLMT1\n",) if backend == "bergamot" else (b"BLCT2\n", b"BLCT3\n")
                if self.read_exact(6) not in expected:
                    raise RuntimeError("Invalid worker handshake")
                self.ready = {"ready": True}
            else:
                self.ready = self.read_line()
                if not self.ready.get("ready"):
                    raise RuntimeError("Native benchmark driver unavailable")
            self.ready_ms = (time.perf_counter() - self.start) * 1000
        except Exception:
            self.close()
            raise

    def read_line(self):
        line = self.reader.submit(self.process.stdout.readline).result(timeout=95)
        if not line:
            raise RuntimeError("Benchmark driver exited")
        return json.loads(line.decode("utf8"))

    def read_exact(self, n):
        def read():
            data = bytearray()
            while len(data) < n:
                part = self.process.stdout.read(n-len(data))
                if not part:
                    raise RuntimeError("Worker pipe ended early")
                data.extend(part)
            return bytes(data)
        return self.reader.submit(read).result(timeout=95)

    def call(self, direction, text):
        start = time.perf_counter()
        if self.raw:
            data = text.encode("utf8")
            index = int(direction == "zh-en")
            if self.backend == "bergamot":
                model = str(self.model_root / ("en-zh-Hans" if not index else "zh-Hans-en")).encode("utf8")
                # Raw timing excludes the native adapter's HTML link protection.
                body = struct.pack("<BBII", index, 0, len(model), len(data)) + model + data
            else:
                body = struct.pack("<BI", index, len(data)) + data
            self.process.stdin.write(body)
            self.process.stdin.flush()
            status, size = struct.unpack("<BI", self.read_exact(5))
            if size > 120000:
                raise RuntimeError("Oversized benchmark response")
            translated = self.read_exact(size).decode("utf8")
            result = {"ok": not status, "text": translated}
        else:
            self.process.stdin.write((json.dumps({"text": text, "direction": direction}, ensure_ascii=False)+"\n").encode("utf8"))
            self.process.stdin.flush()
            result = self.read_line()
        result["wall_ms"] = (time.perf_counter()-start)*1000
        result.setdefault("ms", result["wall_ms"])
        return result

    def close(self):
        if getattr(self, "process", None):
            try:
                self.process.stdin.close()
                self.process.wait(timeout=4)
            except (OSError, subprocess.TimeoutExpired):
                try:
                    children = psutil.Process(self.process.pid).children(recursive=True)
                except psutil.Error:
                    children = []
                for process in children:
                    try:
                        process.terminate()
                    except psutil.Error:
                        pass
                self.process.kill()
                self.process.wait(timeout=4)
            self.monitor.close()
            self.reader.shutdown(wait=True, cancel_futures=True)


def summarize(samples, resources, elapsed, cpu):
    valid = [x for x in samples if x.get("ok")]
    times = [x["ms"] for x in valid]
    return {"requests": len(samples), "success": len(valid),
            "median_ms": statistics.median(times) if times else None,
            "p95_ms": percentile(times, .95) if times else None,
            "max_ms": max(times) if times else None,
            "cpu_ms_per_request": cpu / len(samples),
            "cpu_pct_machine": cpu / elapsed / psutil.cpu_count() * 100,
            "median_rss_mib": statistics.median(x["rss_mib"] for x in resources),
            "median_private_mib": statistics.median(x["private_mib"] for x in resources),
            "sampled_peak_rss_mib": max(x["sampled_peak_rss_mib"] for x in resources),
            "by_length": {length: {"n": len(values), "median_ms": statistics.median(values),
                                   "p95_ms": percentile(values, .95)}
                          for length in ("short", "medium", "long")
                          if (values := [x["ms"] for x in valid if x["length"] == length])}}


def run_one(factory, repeats, backend, cold_repeats):
    cold = []
    for _ in range(cold_repeats):
        driver = factory()
        try:
            result = driver.call("en-zh", CORPUS["en-zh"][0][1])
            cold.append({**result, "process_start_to_result_ms": driver.ready_ms+result["wall_ms"],
                         "resources": driver.monitor.collect()})
        finally:
            driver.close()
    driver = factory()
    output = {"backend": backend, "cold": cold, "ready_ms": driver.ready_ms,
              "model": driver.ready.get("model", backend), "directions": {}}
    try:
        for direction, corpus in CORPUS.items():
            first = driver.call(direction, corpus[0][1])
            if not first.get("ok"):
                output["error"] = first.get("error", first.get("text", "Translation failed"))
                break
            before = driver.monitor.collect()
            start = time.perf_counter()
            samples, resources = [], []
            for repeat in range(repeats):
                for length, text in corpus:
                    result = driver.call(direction, text)
                    samples.append({**result, "input": text, "length": length, "round": repeat})
                    resources.append(driver.monitor.collect())
            elapsed = (time.perf_counter()-start)*1000
            after = driver.monitor.collect()
            output["directions"][direction] = {
                "first_or_direction_switch": first,
                "summary": summarize(samples, resources, elapsed, after["cpu_ms"]-before["cpu_ms"]),
                "samples": samples}
            print(backend, direction, json.dumps(output["directions"][direction]["summary"]), flush=True)
        before = driver.monitor.collect()
        start = time.perf_counter()
        time.sleep(3)
        after = driver.monitor.collect()
        output["idle_loaded"] = {**after, "seconds": time.perf_counter()-start,
                                 "cpu_pct_machine": (after["cpu_ms"]-before["cpu_ms"])/(time.perf_counter()-start)/1000/psutil.cpu_count()*100}
    finally:
        driver.close()
    return output


def run_paced(factory, backend):
    output = {"backend": backend, "paced": []}
    driver = factory()
    try:
        warm_up = driver.call("en-zh", CORPUS["en-zh"][0][1])
        output["warm_up"] = warm_up
        if not warm_up.get("ok"):
            raise RuntimeError("Paced warm-up failed")
        for rate in ([1] if backend == "api" else [1, 5]):
            count = 10
            samples = []
            before = driver.monitor.collect()
            start = time.perf_counter()
            for i in range(count):
                wait = start + i / rate - time.perf_counter()
                if wait > 0:
                    time.sleep(wait)
                result = driver.call("en-zh", CORPUS["en-zh"][i % len(CORPUS["en-zh"])][1])
                samples.append(result)
            wait = start + count / rate - time.perf_counter()
            if wait > 0:
                time.sleep(wait)
            after = driver.monitor.collect()
            elapsed = time.perf_counter() - start
            cpu = after["cpu_ms"] - before["cpu_ms"]
            output["paced"].append({"target_messages_per_second": rate, "requests": count,
                                    "actual_messages_per_second": count / elapsed,
                                    "cpu_pct_machine": cpu / 1000 / elapsed / psutil.cpu_count() * 100,
                                    "cpu_ms_per_message": cpu / count,
                                    "resources_after": after, "samples": samples})
            print(backend, "paced", rate, "CPU%", cpu/1000/elapsed/psutil.cpu_count()*100, flush=True)
            # Observe the quiet state after worker spin-down, separately from
            # the first three seconds immediately following the burst.
            time.sleep(2)
        before = driver.monitor.collect()
        start = time.perf_counter()
        time.sleep(3)
        after = driver.monitor.collect()
        output["settled_idle"] = {"rss_mib": after["rss_mib"], "private_mib": after["private_mib"],
                                  "cpu_pct_machine": (after["cpu_ms"]-before["cpu_ms"])/1000/(time.perf_counter()-start)/psutil.cpu_count()*100}
    finally:
        driver.close()
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backends", nargs="+", default=["bergamot", "ctranslate2", "api"])
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--mode", choices=["connection", "raw", "paced"], default="connection")
    parser.add_argument("--model-root", type=Path, help="decompressed Bergamot model directory for raw mode")
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    result = {"date": datetime.now().astimezone().date().isoformat(),
              "started_at": datetime.now().astimezone().isoformat(),
              "observer_python": sys.version.split()[0], "observer_psutil": psutil.__version__,
              "logical_processors": psutil.cpu_count(),
              "ram_gib": psutil.virtual_memory().total/2**30,
              "observer_baseline_cpu_pct": psutil.cpu_percent(interval=2),
              "scope": args.mode + "; no capture, UI, filtering, translation cache or glossary shortcuts",
              "thread_environment": {name: os.environ.get(name) for name in
                                     ("OMP_WAIT_POLICY", "KMP_BLOCKTIME", "OMP_NUM_THREADS")},
              "sampling_ms": 20, "results": []}
    for backend in args.backends:
        try:
            factory = lambda: Driver([str(args.driver.resolve()), backend], backend)
            if args.mode == "raw":
                if backend not in ("bergamot", "ctranslate2") or not args.model_root:
                    raise ValueError("Raw mode requires a local worker and --model-root")
                root = args.driver.resolve().parent / "data" / backend
                command = ([str(root / "BianLingoBergamot.exe")] if backend == "bergamot" else
                           [str(root / "BianLingoCT2.exe"), "--models-root", str(args.model_root.resolve()), "--bundled-root", str(root), "--catalog", str(Path(__file__).resolve().parents[1]/"resources/ctranslate2-models.json")])
                factory = lambda: Driver(command, backend, True, args.model_root.resolve())
            result["results"].append(run_paced(factory, backend) if args.mode == "paced" else
                                      run_one(factory, args.repeats if backend not in ("api", "ollama") else 1,
                                              backend, 3 if backend not in ("api", "ollama") else 1))
        except Exception as error:
            result["results"].append({"backend": backend, "error": str(error)})
        args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False), encoding="utf8")
    print("Benchmark results:", args.output, flush=True)


if __name__ == "__main__":
    main()
