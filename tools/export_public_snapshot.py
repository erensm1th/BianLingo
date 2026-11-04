"""Export an allowlisted source archive without local/user files.

This is a packaging safeguard, not secret-scanner or copyright clearance. The
formal evidence gate and a human review are still required before publication.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
ROOT_FILES = {'CMakeLists.txt', 'vcpkg.json', '.gitignore', '.gitattributes', 'README.md', 'README.zh-CN.md', 'README.zh-TW.md', 'README.ja.md', 'README.ko.md',
              'README.fr.md', 'README.de.md', 'README.es.md', 'README.pt-BR.md', 'README.ru.md',
              'LICENSE', 'THIRD_PARTY_NOTICES.md', 'PRIVACY.md', 'PROJECT_NOTICE.md', 'SOURCE_ACCESS.md'}
ROOTS = {'src', 'include', 'addon', 'resources', 'cmake', 'tools', 'tests', '.github'}
DOCS = {'BUILDING.md','DEVELOPMENT.md','BERGAMOT_TRANSLATION.md', 'CHAT_TABS.md', 'CTRANSLATE2_TRANSLATION.md',
        'LOCAL_TRANSLATION.md', 'MATRIX_TRANSPORT.md', 'MESSAGE_ACTIONS_AND_BLOCKLIST.md',
        'OCR_FALLBACK.md', 'PERFORMANCE_BENCHMARKS.md', 'PORTABLE_STORAGE.md', 'PROVIDERS.md',
        'RELEASE_COMPLIANCE.md', 'RELEASE_READINESS.md', 'SETTINGS_LAYOUT.md', 'SKINS.md',
        'audits/2026-11-03-ct2-model-licenses.json', 'audits/2026-11-03-native-dependencies.json'}
EXTENSIONS = {'.cpp','.hpp','.h','.mm','.json','.lua','.toc','.md','.py','.ps1','.yml','.yaml',
              '.txt','.in','.inc','.rc','.ico','.png','.tga','.manifest','.natvis','.cmake','.gitignore','.gitattributes'}

def allowed(name):
    path = PurePosixPath(name)
    if not name or not path.parts: raise ValueError('Empty source path')
    if path.name == 'AGENTS.md': return False
    if path.is_absolute() or '..' in path.parts or '\\' in name or ':' in name:
        raise ValueError('Unsafe source path')
    if name in ROOT_FILES: return True
    if re.fullmatch(r'tests/fixtures/bridge-[0-3]-[0-2]\.bgra', name): return True
    if name.startswith('resources/model-evidence/ollama/lib/ollama/') and path.name in {
            'GO_LICENSE','CPP_HTTPLIB_LICENSE','LLAMA_CPP_LICENSE','LLAMA_CPP_VENDORS_LICENSE'}:return True
    if path.parts[0] == 'docs': return path.relative_to('docs').as_posix() in DOCS
    if path.parts[0] not in ROOTS: return False
    if any(p in {'.git','.local','UserData','__pycache__','dist'} for p in path.parts):
        raise ValueError('Private directory inside source roots')
    if path.suffix not in EXTENSIONS and path.name not in {'LICENSE','CMakeLists.txt'}:
        raise ValueError('Unexpected file type in source roots: ' + name)
    return True


def public_bytes(root,name):
    data=(root/name).read_bytes()
    # Match the public text checkout while preserving byte-pinned vendor evidence.
    pinned=name.startswith(('resources/model-evidence/','resources/data/provenance-source/')) or \
           name.startswith('resources/licenses/') and Path(name).suffix=='.txt'
    if Path(name).suffix not in ('.ico','.bgra','.png','.tga') and not pinned:
        data=data.replace(b'\r\n',b'\n')
    return data


def source_digest(root, names=None):
    """Bind review to eligible source bytes; exclude only the self-referential record."""
    if names is None:
        if (root/'.git').exists():
            names=subprocess.check_output(['git','ls-files','-z'],cwd=root).decode('utf8').split('\0')
        else:
            names=[p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file() and
                   not any(x in {'.git','__pycache__','UserData','.local','dist'} for x in p.relative_to(root).parts)]
    records=[]
    for name in sorted(set(names)):
        if not name or not allowed(name) or name=='resources/release-review.json':continue
        data=public_bytes(root,name)
        records.append(dict(path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
    return hashlib.sha256(json.dumps(records,sort_keys=True,separators=(',',':')).encode()).hexdigest()


def inspect_snapshot(path):
    """Separate integrity/secret-pattern pass over the actual generated archive."""
    patterns=[rb'-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----',rb'gh[pousr]_[A-Za-z0-9]{20,}',
              rb'github_pat_[A-Za-z0-9_]{20,}',rb'sk-(?:proj-)?[A-Za-z0-9_-]{30,}',rb'AKIA[A-Z0-9]{16}']
    with ZipFile(path) as archive:
        names=archive.namelist();manifest=json.loads(archive.read('SOURCE_SNAPSHOT.json'))
        expected={x['path']:x for x in manifest['files']}
        if len(names)!=len(set(names)) or set(names)!=set(expected)|{'SOURCE_SNAPSHOT.json'}:
            raise ValueError('Snapshot entry/manifest mismatch')
        for name,item in expected.items():
            if not allowed(name):raise ValueError('Private entry in snapshot')
            data=archive.read(name)
            if len(data)!=item['size'] or hashlib.sha256(data).hexdigest()!=item['sha256']:
                raise ValueError('Snapshot content mismatch')
            if any(re.search(pattern,data) for pattern in patterns):raise ValueError('Potential secret in source: '+name)
    return dict(files=len(expected),sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                scope='Actual archive integrity and known secret patterns; not an exhaustive legal/security certification')

def build_snapshot(root, tracked, output, revision):
    selected = {}
    for name in tracked:
        if not allowed(name): continue
        path = root/name
        resolved = path.resolve(strict=True)
        if not resolved.is_relative_to(root.resolve()) or path.is_symlink():
            raise ValueError('Source links are not exportable: ' + name)
        data = public_bytes(root,name)
        if b'-----BEGIN ' in data and re.search(rb'-----BEGIN (?:[A-Z]+ )?PRIVATE KEY-----', data):
            raise ValueError('Private-key material detected: ' + name)
        selected[name] = data
    for required in ('LICENSE','CMakeLists.txt','README.md','resources/release-review.json'):
        if required not in selected: raise ValueError('Missing public source input: ' + required)
    review = json.loads(selected['resources/release-review.json'])
    manifest = dict(schemaVersion=1, kind='source-review-candidate', revision=revision,
        notice='Source integrity manifest. Packaging checks are not exhaustive security certification.',
        releaseReview=review,
        files=[dict(path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest())
               for name,data in sorted(selected.items())])
    output.parent.mkdir(parents=True,exist_ok=True)
    # Exclusive creation protects an existing reviewed artifact from replacement.
    with ZipFile(output,'x',compression=ZIP_DEFLATED) as archive:
        for name,data in sorted(selected.items()): archive.writestr(name,data)
        archive.writestr('SOURCE_SNAPSHOT.json',json.dumps(manifest,ensure_ascii=False,indent=2)+'\n')
    return manifest

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if subprocess.check_output(['git','status','--porcelain'],cwd=ROOT):
        raise ValueError('Commit and review the worktree before exporting a public source candidate')
    from compliance import validate_sources
    validate_sources()
    names=subprocess.check_output(['git','ls-files','-z'],cwd=ROOT).decode('utf8').split('\0')
    revision=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    manifest=build_snapshot(ROOT,[n for n in names if n],args.output,revision)
    print(f"Exported {len(manifest['files'])} files for review; not published: {args.output}")

if __name__=='__main__': main()
