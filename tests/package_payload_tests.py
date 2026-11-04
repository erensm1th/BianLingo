"""Exercise the actual native unpacker with valid and hostile ZIP fixtures."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import subprocess
import tempfile
from zipfile import ZipFile, ZipInfo, ZIP_DEFLATED


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--helper', type=Path, required=True)
    args = parser.parse_args()
    helper = args.helper.resolve()
    with tempfile.TemporaryDirectory(prefix='bianlingo-payload-') as temporary:
        root = Path(temporary)
        destination = root / 'UserData' / 'Package'

        def build(entries, records=None):
            output = io.BytesIO()
            with ZipFile(output, 'w', compression=ZIP_DEFLATED) as z:
                for name, data in entries:
                    z.writestr(name, data)
            payload = output.getvalue()
            files = records if records is not None else {
                (name.filename if isinstance(name, ZipInfo) else name):
                    dict(size=len(data), sha256=hashlib.sha256(data).hexdigest()) for name, data in entries}
            return payload, dict(schema=1, sha256=hashlib.sha256(payload).hexdigest(), files=files)

        def run(payload, metadata, success):
            (root/'payload.zip').write_bytes(payload)
            (root/'metadata.json').write_text(json.dumps(metadata), encoding='utf8')
            result = subprocess.run([str(helper), str(root/'payload.zip'), str(root/'metadata.json'), str(destination)],
                                    capture_output=True, timeout=15)
            assert (result.returncode == 0) == success, result.stderr.decode('utf8', errors='replace')

        entries = [('LICENSE', b'license fixture'), ('data/bergamot/engine-LICENSES.txt', b'native notices')]
        payload, metadata = build(entries)
        run(payload, metadata, True)
        assert (destination/'LICENSE').read_bytes() == entries[0][1]
        # A valid installed cache avoids re-reading the large payload on restart.
        run(b'not read for a complete cache', metadata, True)
        (destination/'.package-ready.json').write_text('{"sha256":123,"metadata_sha256":false}')
        run(payload, metadata, True)
        (destination/'LICENSE').write_bytes(b'truncated')
        run(payload, metadata, True)
        assert (destination/'LICENSE').read_bytes() == entries[0][1]
        for invalid in ['../escape', '/escape', 'data/bergamot/../../escape', 'data/bergamot/CON.txt',
                        'data/bergamot/name:stream', 'data/bergamot/name.', 'data\\bergamot\\escape']:
            run(*build([(invalid,b'bad')]), False)
            assert (destination/'LICENSE').read_bytes() == entries[0][1]
        run(*build(entries + [('license',b'duplicate case')]), False)
        run(*build(entries + [entries[0]]), False)
        link = ZipInfo('data/bergamot/link');link.create_system=3;link.external_attr=(0o120777 << 16)
        run(*build([(link,b'../../escape')]), False)
        corrupt = dict(metadata);corrupt['sha256']='0'*64
        run(payload, corrupt, False)
        missing = json.loads(json.dumps(metadata));missing['files']['LICENSE']['sha256']='1'*64
        missing['sha256']='2'*64
        run(payload, missing, False)
        wrong_files = json.loads(json.dumps(metadata['files']));wrong_files['LICENSE']['sha256']='1'*64
        run(*build(entries,wrong_files), False)
        wrong_files['LICENSE']['size']=256*1024*1024
        run(*build(entries,wrong_files), False)
        run(*build(entries[:-1], metadata['files']), False)
        assert not (root/'escape').exists()
        assert not list((root/'UserData').glob('.package-staged-*'))
        assert (destination/'LICENSE').read_bytes() == entries[0][1]
    print('PASS native extraction, cache reuse/repair, bounds, traversal, links, duplicates, integrity and rollback')


if __name__ == '__main__':
    main()
