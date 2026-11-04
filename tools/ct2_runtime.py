"""Check the shipped CPU worker, not just its self-reported build metadata."""
import hashlib
import json
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
FILES = ('BianLingoCT2.exe', 'engine.sha256', 'native-build.json', 'engine-LICENSES.txt')
SYSTEM_DLLS = {'kernel32.dll', 'advapi32.dll', 'shell32.dll', 'bcrypt.dll', 'dbghelp.dll'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def pe_imports(path):
    data = path.read_bytes()
    def read(fmt, offset):
        if offset < 0 or offset + struct.calcsize(fmt) > len(data):
            raise ValueError('Truncated PE image')
        return struct.unpack_from(fmt, data, offset)
    if data[:2] != b'MZ': raise ValueError('Worker is not a PE image')
    pe, = read('<I', 0x3c)
    if data[pe:pe+4] != b'PE\0\0': raise ValueError('Invalid PE signature')
    machine, count = read('<HH', pe+4)
    if machine != 0x8664 or not 1 <= count <= 96: raise ValueError('Expected x64 worker')
    opt_size, = read('<H', pe+20)
    opt = pe+24
    if read('<H', opt)[0] != 0x20b or opt_size < 240: raise ValueError('Invalid PE optional header')
    image_base, = read('<Q', opt+24)
    sections = [read('<IIII', opt+opt_size+40*i+8) for i in range(count)]
    def offset(rva, length=1):
        for virtual, address, raw_size, raw in sections:
            if address <= rva and rva+length <= address+raw_size:
                result = raw+rva-address
                if result+length <= len(data): return result
        raise ValueError('PE directory extends outside sections')
    def name(rva):
        begin = offset(rva); end = data.find(b'\0', begin, min(begin+256,len(data)))
        if end < 0: raise ValueError('Unterminated PE import')
        return data[begin:end].decode('ascii').lower()
    result = set()
    for directory, width in ((1,20), (13,32)):
        rva, size = read('<II', opt+112+8*directory)
        if not rva:
            if size: raise ValueError('Invalid empty PE directory')
            continue
        base = offset(rva,size)
        if size < width: raise ValueError('Truncated PE import directory')
        ended = False
        for i in range(size//width):
            row = read('<'+'I'*(width//4), base+width*i)
            if not any(row): ended=True; break
            target = row[3] if directory == 1 else row[1]
            if directory == 13 and not row[0]&1: target -= image_base
            result.add(name(target))
        if not ended: raise ValueError('Unterminated PE import table')
    return sorted(result)


def validate_runtime(directory, root=ROOT):
    if any(not (directory/name).is_file() or (directory/name).is_symlink() for name in FILES):
        raise ValueError('Native CT2 package is incomplete')
    manifest=json.loads((directory/'native-build.json').read_text(encoding='utf8'))
    runtime=json.loads((root/'resources/ctranslate2-runtime.json').read_text(encoding='utf8'))
    lock=json.loads((root/'resources/ct2-native-sources.json').read_text(encoding='utf8'))
    signature=hashlib.sha256(json.dumps(lock,sort_keys=True).encode()).hexdigest()
    binary=sha(directory/'BianLingoCT2.exe')
    if manifest.get('protocol')!='BLCT3' or manifest.get('binary_sha256')!=binary or \
       (directory/'engine.sha256').read_text(encoding='ascii').strip()!=binary or \
       manifest.get('source_lock_sha256')!=signature or runtime.get('source_lock_sha256')!=signature:
        raise ValueError('Native CT2 binary/source manifest mismatch')
    inputs=manifest.get('inputs',[])
    if [record.get('source') for record in inputs]!=lock['sources'] or any(not x.get('notices') for x in inputs):
        raise ValueError('Native CT2 source notice inventory mismatch')
    names={'tools/build_ct2_native.py',*(p.relative_to(root).as_posix() for p in (root/'tools/ctranslate2_native').iterdir() if p.is_file())}
    if set(manifest.get('wrapper_sources',{}))!=names or any(hashlib.sha256((root/name).read_text(encoding='utf8').encode('utf8')).hexdigest()!=digest for name,digest in manifest['wrapper_sources'].items()):
        raise ValueError('Native CT2 wrapper source changed; rebuild the worker')
    imports=pe_imports(directory/'BianLingoCT2.exe')
    if not imports or not set(imports)<=SYSTEM_DLLS or imports!=sorted(n.lower() for n in manifest.get('imports',[])):
        raise ValueError('Native CT2 has unexpected runtime DLL dependencies')
    if (directory/'engine-LICENSES.txt').read_bytes()!=(root/'resources/licenses/CT2Native.txt').read_bytes():
        raise ValueError('Native CT2 bundled notices changed; review and refresh source notices')
    return manifest
