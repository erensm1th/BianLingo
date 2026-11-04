"""Co-distribute actual patched helper sources, initialized submodules and PCRE2.

Uses Git's tracked-file inventory, never a recursive copy of a developer workspace.
Does not include .git, caches, weights, settings, private history or credentials.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import os
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
COMMIT = '9271618ebbdc5d21ac4dc4df9e72beb7ce644774'

def git(source, *args):
    return subprocess.check_output(['git','-C',str(source),*args])


def source_data(root,path,link_blob=None):
    """Materialize tracked internal links consistently on Windows and Unix."""
    target=None
    if link_blob:
        physical=path.is_symlink()
        raw=os.fsencode(os.readlink(path)) if physical else path.read_bytes()
        # Win32 stores relative symlink separators as backslashes even when
        # Git's original blob used '/'. Compare their canonical Git spelling.
        if physical and os.name=='nt':raw=raw.replace(b'\\',b'/')
        if hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()!=link_blob:
            raise ValueError('Tracked source link was modified: '+str(path))
        target=raw.decode('utf8')
        destination=(path.parent/target).resolve(strict=True)
    else:
        if path.is_symlink():raise ValueError('Untracked source link: '+str(path))
        destination=path.resolve(strict=True)
    if not destination.is_relative_to(root.resolve()) or not destination.is_file():
        raise ValueError('Source entry escapes its root or is not a file: '+str(path))
    return destination.read_bytes(),target

def export_sources(source, runtime):
    source, runtime = source.resolve(), runtime.resolve()
    if git(source,'rev-parse','HEAD').decode().strip() != COMMIT:
        raise ValueError('Unexpected helper source revision')
    binary_hash = hashlib.sha256((runtime/'BianLingoBergamot.exe').read_bytes()).hexdigest()
    # Every recursively initialized submodule is included at its actual revision.
    status = git(source,'submodule','status','--recursive').decode()
    if any(line.startswith(('+','U')) for line in status.splitlines()):
        raise ValueError('Submodule revision differs from pinned source')
    files = {}; links={}
    for entry in git(source,'ls-files','--stage','--recurse-submodules','-z').decode().split('\0'):
        if not entry:continue
        metadata,name=entry.split('\t',1);mode,blob,stage=metadata.split()
        if stage!='0':raise ValueError('Unmerged source entry')
        path=source/name
        if mode=='120000':links['bergamot-source/'+name]=blob
        if path.is_file() or mode=='120000':files['bergamot-source/'+name]=path
    pcre=source/'3rd_party/ssplit-cpp/src/3rd-party/pcre2-10.39'
    if not (pcre/'configure').is_file(): raise ValueError('Corresponding PCRE2 source is missing')
    for path in pcre.rglob('*'):
        if path.is_file(): files['bergamot-source/'+path.relative_to(source).as_posix()]=path
    header=source/'3rd_party/marian-dev/src/common/git_revision.h'
    if not header.is_file(): raise ValueError('Actual generated Marian version header is missing')
    files['bergamot-source/'+header.relative_to(source).as_posix()]=header
    for name in ('build_bergamot.py','export_bergamot_source.py','bergamot_helper/CMakeLists.txt','bergamot_helper/src/main.cpp'):
        files['project/tools/'+name]=ROOT/'tools'/name
    files['project/LICENSE']=ROOT/'LICENSE'
    output=runtime/'corresponding-source.zip'
    records=[];materialized=[]
    with ZipFile(output,'w',ZIP_DEFLATED,compresslevel=6) as archive:
        for name,path in sorted(files.items()):
            if any(p in {'.git','UserData','__pycache__'} for p in Path(name).parts):
                raise ValueError(f'Unsafe source entry: {name}')
            data,target=source_data(source if name.startswith('bergamot-source/') else ROOT,path,links.get(name))
            if target is not None:materialized.append(dict(path=name,target=target))
            archive.writestr(name,data)
            records.append(dict(path=name,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
        readme=('Corresponding source for BianLingoBergamot.exe, SHA-256 '+binary_hash+'\n'
            'Bergamot revision: '+COMMIT+'\nInitialized submodules:\n'+status+'\n'
            'This archive contains the actual patched files, not only upstream links.\n'
            'Tracked links inside the source tree are materialized as regular files; SOURCE_FILES.json records their original targets. External/untracked links are rejected.\n'
            'Covered upstream files retain MPL-2.0 and their original notices; third-party submodules retain their own terms.\n'
            'The independent wrapper/build scripts retain project MIT. No source rights are restricted by project binary terms.\n'
            'Build on Windows with VS2022 C++ x64, Windows SDK and CMake 3.31.10.\n'
            'cmake -S project/tools/bergamot_helper -B build -A x64 -DBL_BERGAMOT_SOURCE=<absolute-path-to-bergamot-source>\n'
            'cmake --build build --config Release --target BianLingoBergamot --parallel 3\n'
            'The pre-patched tree and included PCRE2 source avoid needing the private application repository.\n')
        archive.writestr('README.txt',readme)
        archive.writestr('SOURCE_FILES.json',json.dumps(dict(binary_sha256=binary_hash,files=records,materialized_links=materialized),indent=2)+'\n')
    info=dict(filename=output.name,sha256=hashlib.sha256(output.read_bytes()).hexdigest(),binary_sha256=binary_hash,
        bergamot_revision=COMMIT,files=len(records))
    (runtime/'source-manifest.json').write_text(json.dumps(info,indent=2)+'\n',encoding='utf8')
    print(f'Exported {len(records)} source files: {output.stat().st_size} bytes')
    return info

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);p.add_argument('--runtime',type=Path,required=True)
    a=p.parse_args();export_sources(a.source,a.runtime)
