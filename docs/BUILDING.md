# Building BianLingo on Windows

Use Visual Studio 2022 with Desktop C++, a Windows SDK, CMake 3.31.10, Git and Python 3.13. Python is a development dependency, not an application runtime.

```powershell
python -m pip install lupa==2.8 zstandard==0.25.0
git clone https://github.com/microsoft/vcpkg.git .tools/vcpkg
git -C .tools/vcpkg checkout 469a0504e4a6ebf6173d1c0f51a7fa34c382fd43
.tools/vcpkg/bootstrap-vcpkg.bat -disableMetrics
$env:CMAKE_GENERATOR = "Visual Studio 17 2022"
python tools/build_bergamot.py --source .tools/bergamot-source --build .tools/bergamot-build --output .local/bergamot-runtime
python tools/prepare_bergamot.py --output .local/bergamot-runtime
python tools/build_ct2_native.py --source .tools/ct2-source --build .tools/ct2-build --output .local/ct2-runtime --cache .tools/ct2-cache
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="$PWD/.tools/vcpkg/scripts/buildsystems/vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static -DBL_BERGAMOT_RUNTIME="$PWD/.local/bergamot-runtime" -DBL_CT2_NATIVE_RUNTIME="$PWD/.local/ct2-runtime"
cmake --build build --config Release --parallel 3
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix dist/windows-x64
python tools/package_windows.py --root dist/windows-x64 --output dist/BianLingo-windows-x64.zip
```

The builders verify fixed source archives and retain component notices. Windows dependencies use a static CRT. The output ZIP contains `BianLingo.exe`, `data` and `AddOns`. Runtime components, models and corresponding source live in `data`; full policies and package records live in `data/Notices`. Copy `AddOns/BianLingo` into the game client.

Keep the package stage clean: inference integration tests run in a separate fixture because they create `UserData`. Never include personal settings, keys, logs or caches in a distribution. `--formal` additionally checks the reviewed source fingerprint and release evidence. The Windows workflow builds artifacts without publishing a Release.

Committed PNG, ICO and TGA assets provide application, settings and addon icons. Artwork sources are documented in [branding](../resources/branding/README.md).

## Standalone addon

`python tools/package_addon.py --output dist/BianLingo-addon.zip` creates a CurseForge-ready archive with a single `BianLingo/` folder and no desktop binaries or models. Select the supported game versions when uploading to CurseForge; archive generation does not publish it.

Client-specific TOCs cover Forever (16001), Retail (120100/120105), Classic Era (11509), Burning Crusade/Anniversary (20506), Mists (50504), and retained Wrath (30403) and Cataclysm (40402) compatibility. The Camelot TOC retains Forever beta compatibility. TOC acceptance does not establish in-game validation of every client; protected chat remains subject to Blizzard restrictions.
