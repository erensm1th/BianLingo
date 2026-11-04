"""Check the delivered PE payload and exercise an isolated first launch/restart."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
import time
from zipfile import ZipFile
from windows_bundle import read_resources


def verify(archive, smoke=False):
    with tempfile.TemporaryDirectory(prefix='.bianlingo-verify-', dir=archive.resolve().parent) as temporary:
        root = Path(temporary)
        with ZipFile(archive) as z:
            names = z.namelist()
            assert len(names) == len(set(names))
            assert {n.split('/')[0] for n in names} == {'BianLingo.exe','AddOns','data'}
            assert 'AddOns/BianLingo/BianLingo.toc' in names
            assert 'data/bergamot/corresponding-source.zip' in names
            assert not any(Path(n).is_absolute() or '..' in Path(n).parts for n in names)
            z.extractall(root)
        metadata = json.loads((root/'data/Notices/artifact-inventory.json').read_text(encoding='utf8'))
        for record in metadata['files']:
            name = record['path']
            target = root/name if name.startswith('data/') else root/'data/Notices'/name
            data = target.read_bytes()
            assert len(data) == record['size'] and hashlib.sha256(data).hexdigest() == record['sha256']
        if smoke:
            startup = subprocess.STARTUPINFO()
            startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
            startup.wShowWindow = 0
            for label in ['first launch', 'cached restart']:
                started = time.monotonic()
                result = subprocess.run([str(root/'BianLingo.exe'),'--smoke-test'], cwd=root,
                                        capture_output=True, timeout=30, startupinfo=startup)
                assert result.returncode == 0, (label, result.stderr)
                print(label, f'{time.monotonic()-started:.2f}s')
                assert {p.name for p in root.iterdir()} == {'BianLingo.exe','AddOns','data','UserData'}
    print('PASS delivered layout, PE resources, payload integrity, notices and matching source')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--archive', type=Path, required=True)
    parser.add_argument('--smoke', action='store_true')
    args = parser.parse_args()
    verify(args.archive, args.smoke)
