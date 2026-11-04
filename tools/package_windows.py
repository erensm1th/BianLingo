"""Create a Windows preview ZIP with the complete local runtime and license files."""

import argparse
import json
import shutil
import tempfile
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile
from ct2_runtime import validate_runtime, FILES as CT2_FILES
from compliance import validate_sources, validate_source_archive, formal_gate, inventory, digest
from windows_bundle import make_payload, embed_resources


ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--formal", action="store_true", help="Require all recorded release reviews to be complete")
    args = parser.parse_args()
    validate_sources()
    if args.formal: formal_gate()
    validate_source_archive(args.root)
    models = json.loads((ROOT / "resources" / "bergamot-models.json").read_text(encoding="utf8"))
    expected = {
        "BianLingo.exe",
        *("data/ctranslate2/"+name for name in CT2_FILES),
        "LICENSE",
        "THIRD_PARTY_NOTICES.md",
        "PROJECT_NOTICE.md",
        "PRIVACY.md", "SOURCE_ACCESS.md", "RELEASE_REVIEW.json", "artifact-inventory.json",
        *("BianLingo/" + p.name for p in (ROOT / "addon/BianLingo").glob("*.toc")),
        "BianLingo/PixelBridge.lua",
        "BianLingo/LocaleMessages.lua",
        "BianLingo/TraditionalMarkers.lua",
        "BianLingo/Blocklist.lua",
        "BianLingo/Textures/Logo.tga",
        "data/bergamot/models.json",
        "data/bergamot/BianLingoBergamot.exe",
        "data/bergamot/engine.sha256",
        "data/bergamot/engine-LICENSES.txt",
        "data/bergamot/corresponding-source.zip", "data/bergamot/source-manifest.json",
        *(f'data/bergamot/compressed/{m["pair"]}/{m["name"]}.zst' for m in models["models"] if "compressed_sha256" in m),
    }
    actual = {p.relative_to(args.root).as_posix() for p in args.root.rglob("*") if p.is_file()}
    # Reject personal data BEFORE generating an inventory; do not copy or enumerate
    # private settings into a shareable report, even when packaging is later rejected.
    if any(p.is_symlink() for p in args.root.rglob('*')): raise ValueError('Linked package entries are forbidden')
    if actual - (expected | {'native-build.json'}): raise ValueError('Unexpected files in package: personal data is never allowed')
    for installed, source in [('LICENSE','LICENSE'),('THIRD_PARTY_NOTICES.md','THIRD_PARTY_NOTICES.md'),
                              ('PROJECT_NOTICE.md','PROJECT_NOTICE.md'),('PRIVACY.md','PRIVACY.md'),('SOURCE_ACCESS.md','SOURCE_ACCESS.md'),
                              ('RELEASE_REVIEW.json','resources/release-review.json'),
                              ('BianLingo/Textures/Logo.tga','addon/BianLingo/Textures/Logo.tga'),
                              ('data/bergamot/models.json','resources/bergamot-models.json')]:
        if (args.root/installed).read_bytes()!=(ROOT/source).read_bytes():
            raise ValueError('Stale package source/notice/review file: '+installed)
    for source in (ROOT/'addon/BianLingo').glob('*.toc'):
        if (args.root/'BianLingo'/source.name).read_bytes() != source.read_bytes():
            raise ValueError('Stale addon TOC: ' + source.name)
    for model in models['models']:
        if 'compressed_sha256' in model:
            path=args.root/f'data/bergamot/compressed/{model["pair"]}/{model["name"]}.zst'
            if digest(path)!=model['compressed_sha256']: raise ValueError('Bundled compressed model hash mismatch')
    validate_runtime(args.root/"data/ctranslate2")
    helper=digest(args.root/'data/bergamot/BianLingoBergamot.exe')
    if (args.root/'data/bergamot/engine.sha256').read_text(encoding='ascii').strip()!=helper:
        raise ValueError('Helper engine.sha256 mismatch')
    report = inventory(args.root)
    report['kind'] = 'embedded-runtime-and-notice-inventory'
    report['files'] = [f for f in report['files'] if f['path'] != 'BianLingo.exe' and not f['path'].startswith('BianLingo/')]
    (args.root/'artifact-inventory.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    actual.add('artifact-inventory.json')
    if actual != expected | {"native-build.json"}:
        raise ValueError(f"Unexpected Windows package layout: {sorted(actual ^ (expected | {'native-build.json'}))}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.bianlingo-package-', dir=args.output.parent) as temporary:
        candidate = Path(temporary) / 'package.zip'
        with ZipFile(candidate, 'w', compression=ZIP_DEFLATED, compresslevel=6) as zipped:
            for name in sorted(expected):
                target = ('AddOns/' + name if name.startswith('BianLingo/') else
                          name if name == 'BianLingo.exe' or name.startswith('data/') else
                          'data/Notices/' + name)
                zipped.write(args.root / name, target)
            for name in ['LICENSE']:
                zipped.write(args.root / name, 'AddOns/BianLingo/' + name)
            licenses = [ROOT/'LICENSE', ROOT/'THIRD_PARTY_NOTICES.md', *sorted((ROOT/'resources/licenses').glob('*.txt')), *sorted((ROOT/'resources/data/licenses').glob('*.txt'))]
            complete = b'\n\n'.join(b'=== '+p.name.encode('utf8')+b' ===\n'+p.read_bytes() for p in licenses)
            zipped.writestr('data/Notices/COMPONENT_LICENSES.txt', complete)
        candidate.replace(args.output)



if __name__ == "__main__":
    main()
