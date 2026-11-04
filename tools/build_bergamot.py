"""Build our pipe helper against pinned Bergamot source with recorded MSVC patches."""

import argparse
import hashlib
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
BERGAMOT_URL = "https://github.com/browsermt/bergamot-translator.git"
BERGAMOT_TAG = "main"
BERGAMOT_COMMIT = "9271618ebbdc5d21ac4dc4df9e72beb7ce644774"


def run(*args: str) -> None:
    subprocess.run(args, check=True)


def patch_exact(path: Path, old: str, new: str) -> None:
    data = path.read_text(encoding="utf-8")
    # A replacement may wrap the old text (protobuf) or be a substring of it
    # (removing GCC pragmas). Only the former proves an already-applied patch.
    if new in data and old in new:
        return
    if data.count(old) == 1:
        path.write_text(data.replace(old, new), encoding="utf-8")
    elif old not in data and new in data:
        return
    else:
        raise ValueError(f"Unexpected upstream source: {path}")


def prepare_source(source: Path) -> None:
    if not (source / ".git").exists():
        source.parent.mkdir(parents=True, exist_ok=True)
        run("git", "-c", "core.symlinks=false", "clone", "--depth", "1", "--branch", BERGAMOT_TAG, BERGAMOT_URL, str(source))
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != BERGAMOT_COMMIT:
        run("git", "-C", str(source), "fetch", "--depth", "1", "origin", BERGAMOT_COMMIT)
        run("git", "-c", "core.symlinks=false", "-C", str(source), "checkout", "--detach", BERGAMOT_COMMIT)
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if actual != BERGAMOT_COMMIT:
        raise ValueError(f"Unexpected Bergamot commit: {actual}")
    # No global Git setting: deterministic checkout with/without Developer Mode.
    # The exporter validates and materializes tracked links as regular files.
    run("git", "-c", "core.symlinks=false", "-C", str(source), "submodule", "update", "--init", "--recursive", "--depth", "1",
        "3rd_party/marian-dev", "3rd_party/ssplit-cpp")

    # The pinned SentencePiece protobuf shim uses MSVC's removed hash_compare.
    shim = source / "3rd_party/marian-dev/src/3rd_party/sentencepiece/third_party/protobuf-lite/google/protobuf/stubs/hash.h"
    anchor = "#if defined(GOOGLE_PROTOBUF_HAS_CXX11_HASH)"
    replacement = ("#if defined(_MSC_VER) && _MSC_VER >= 1900\n"
                   "#undef GOOGLE_PROTOBUF_HAVE_HASH_MAP\n"
                   "#undef GOOGLE_PROTOBUF_HAVE_HASH_SET\n"
                   "#endif\n" + anchor)
    patch_exact(shim, anchor, replacement)

    # PCRE2's CMake install names its Windows static archive pcre2-8-static.lib.
    pcre = source / "3rd_party/ssplit-cpp/cmake/FindPCRE2.cmake"
    patch_exact(pcre, "${CMAKE_STATIC_LIBRARY_PREFIX}pcre2-8${CMAKE_STATIC_LIBRARY_SUFFIX}",
                "${CMAKE_STATIC_LIBRARY_PREFIX}pcre2-8-static${CMAKE_STATIC_LIBRARY_SUFFIX}")
    patch_exact(pcre, "    -DBUILD_SHARED_LIBS=OFF\n",
                "    -DBUILD_SHARED_LIBS=OFF\n    -DPCRE2_STATIC_RUNTIME=ON\n")

    # Marian's Ruy header applies GCC pragmas that MSVC treats as errors.
    ruy = source / "3rd_party/marian-dev/src/tensors/cpu/prod_blas.h"
    old = ('#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored "-Wcomment"\n'
           '    #include "ruy/ruy.h"\n    #include "ruy/system_aligned_alloc.h"\n#pragma GCC pop')
    new = ('    #include "ruy/ruy.h"\n    #include "ruy/system_aligned_alloc.h"')
    patch_exact(ruy, old, new)

    # A distributed source archive has no private .git metadata. Retain the
    # exact generated version header, and allow rebuilding from that source.
    version = source / "3rd_party/marian-dev/src/CMakeLists.txt"
    start = "set(MARIAN_GIT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../.git)"
    patch_exact(version, start, 'if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../.git")\n' + start)
    end = "add_custom_target(marian_version DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/common/git_revision.h)"
    patch_exact(version, end, end + '\nelseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/common/git_revision.h")\n'
                '  add_custom_target(marian_version DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/common/git_revision.h)\n'
                'else()\n  message(FATAL_ERROR "Source archive is missing git_revision.h")\nendif()')


def write_notices(source: Path, output: Path) -> None:
    paths = [source / "LICENSE"]
    paths += sorted(path for path in (source / "3rd_party").rglob("*")
                    if path.is_file() and path.name.lower().startswith(("license", "copying", "notice"))
                    and not any(part in ("examples", "regression-tests", "benchmark", "bench")
                                for part in path.relative_to(source).parts))
    with (output / "engine-LICENSES.txt").open("w", encoding="utf-8") as notices:
        notices.write(f"Bergamot source: {BERGAMOT_URL}\nCommit: {BERGAMOT_COMMIT}\n"
                      "Build compatibility patches: tools/build_bergamot.py\n"
                      "Firefox translation models: https://github.com/mozilla/translations (MPL-2.0)\n\n")
        for path in paths:
            notices.write(f"\n===== {path.relative_to(source).as_posix()} =====\n")
            notices.write(path.read_text(encoding="utf-8", errors="replace"))
            notices.write("\n")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cmake", default="cmake")
    args = parser.parse_args()
    source, build, output = (path.resolve() for path in (args.source, args.build, args.output))
    prepare_source(source)
    run(args.cmake, "-S", str(ROOT / "tools/bergamot_helper"), "-B", str(build),
        "-A", "x64", f"-DBL_BERGAMOT_SOURCE={source}")
    run(args.cmake, "--build", str(build), "--config", "Release", "--target", "BianLingoBergamot",
        "--parallel", "3")
    binary = next(build.rglob("Release/BianLingoBergamot.exe"), None)
    if binary is None:
        raise FileNotFoundError("Own Bergamot helper was not built")
    output.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, output / "BianLingoBergamot.exe")
    (output / "engine.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest() + "\n", encoding="ascii")
    write_notices(source, output)
    from export_bergamot_source import export_sources
    export_sources(source, output)


if __name__ == "__main__":
    main()
