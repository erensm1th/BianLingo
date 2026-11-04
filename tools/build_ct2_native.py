"""Build the pinned CPU-only worker. All downloads/build files use supplied dirs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import os
from urllib.request import urlopen
from zipfile import ZipFile

ROOT=Path(__file__).resolve().parents[1]
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for b in iter(lambda:stream.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def fetch(item,cache):
    name=item['name']+'-source.zip' if item['format']=='zip' else 'sentencepiece-0.2.2.tar.gz'
    target=cache/name
    if target.is_file() and target.stat().st_size==item['size'] and digest(target)==item['sha256']:return target
    cache.mkdir(parents=True,exist_ok=True);temporary=target.with_suffix(target.suffix+'.part')
    with urlopen(item['url'],timeout=90) as response,temporary.open('wb') as out:shutil.copyfileobj(response,out,1024*1024)
    if temporary.stat().st_size!=item['size'] or digest(temporary)!=item['sha256']:raise ValueError('Source SHA mismatch: '+item['name'])
    temporary.replace(target);return target

def unpack(archive,destination,kind):
    destination.mkdir(parents=True,exist_ok=True);root=destination.resolve()
    def target(name):
        relative='/'.join(name.split('/')[1:])
        if not relative:return None
        if '\\' in relative or ':' in relative:raise ValueError('Unsafe source name')
        p=(root/relative).resolve()
        if not p.is_relative_to(root):raise ValueError('Unsafe source path')
        return p
    if kind=='zip':
        with ZipFile(archive) as z:
            for member in z.infolist():
                p=target(member.filename)
                if p is None:continue
                if (member.external_attr>>16)&0xf000==0xa000:raise ValueError('Source symlink rejected')
                if member.is_dir():p.mkdir(parents=True,exist_ok=True)
                else:p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(z.read(member))
    else:
        with tarfile.open(archive) as tar:
            for member in tar:
                p=target(member.name)
                if p is None:continue
                if member.isdir():p.mkdir(parents=True,exist_ok=True)
                elif member.isfile():
                    p.parent.mkdir(parents=True,exist_ok=True)
                    with tar.extractfile(member) as source,p.open('wb') as out:shutil.copyfileobj(source,out)
                else:raise ValueError('Source link/special file rejected')


def verify_source_tree(source,lock,cache):
    """Cached extraction is not evidence: check bytes and reject injected files."""
    expected={}
    for item in lock['sources']:
        base=source/'sentencepiece' if item['name']=='sentencepiece' else source/'ctranslate2'/item['destination']
        archive=fetch(item,cache)
        if item['format']=='zip':
            with ZipFile(archive) as z:
                for member in z.infolist():
                    if not member.is_dir():expected[base/'/'.join(member.filename.split('/')[1:])]=z.read(member)
        else:
            with tarfile.open(archive) as tar:
                for member in tar:
                    if member.isfile():expected[base/'/'.join(member.name.split('/')[1:])]=tar.extractfile(member).read()
    for path,original in expected.items():
        if path.is_symlink() or not path.is_file():raise ValueError('Pinned source missing/linked: '+str(path))
        actual=path.read_bytes()
        if actual==original:continue
        rel=path.relative_to(source).as_posix()
        if rel=='sentencepiece/sentencepiece/CMakeLists.txt':
            patched=re.sub(r'  FetchContent_Populate\(abseil-cpp\s+GIT_REPOSITORY.*?GIT_TAG 20260526\.0\)',
                '  # BianLingo: use the checksum-pinned Abseil source supplied by this sdist.',original.decode('utf8'),flags=re.S)
        elif rel=='ctranslate2/third_party/ruy/ruy/thread_pool.h':
            patched=original.decode('utf8').replace('Duration spin_duration_ = DurationFromMilliseconds(2);',
                'Duration spin_duration_ = DurationFromMilliseconds(0); // BianLingo: passive chat worker')
        else:raise ValueError('Pinned source modified outside recorded patches: '+rel)
        if actual.decode('utf8').replace('\r\n','\n')!=patched.replace('\r\n','\n'):
            raise ValueError('Recorded source patch differs: '+rel)
    prefix='sentencepiece/sentencepiece/third_party/absl/'
    for path in source.rglob('*'):
        if path.is_symlink():raise ValueError('Source symlink rejected')
        if not path.is_file() or path in expected or path==source/'source-lock.json':continue
        rel=path.relative_to(source).as_posix()
        original=source/'sentencepiece/sentencepiece/third_party/abseil-cpp/absl'/rel[len(prefix):] if rel.startswith(prefix) else None
        if original not in expected or path.read_bytes()!=expected[original]:
            raise ValueError('Unrecorded file in build source: '+rel)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--build',type=Path,required=True);parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cache',type=Path,required=True);parser.add_argument('--cmake',default='cmake')
    parser.add_argument('--jobs',type=int,default=4);parser.add_argument('--dumpbin')
    args=parser.parse_args();lock=json.loads((ROOT/'resources/ct2-native-sources.json').read_text(encoding='utf8'))
    source=args.source.resolve();marker=source/'source-lock.json';fingerprint=hashlib.sha256(json.dumps(lock,sort_keys=True).encode()).hexdigest()
    if not marker.exists():
        if source.exists() and any(source.iterdir()):raise ValueError('Use an empty source directory')
        for item in lock['sources']:
            directory=source/'sentencepiece' if item['name']=='sentencepiece' else source/'ctranslate2'/item['destination']
            unpack(fetch(item,args.cache),directory,item['format'])
        marker.write_text(json.dumps({'sha256':fingerprint}),encoding='utf8')
    elif json.loads(marker.read_text())['sha256']!=fingerprint:raise ValueError('Use a new source directory for a changed lock')
    verify_source_tree(source,lock,args.cache)
    spm=source/'sentencepiece'/'sentencepiece'
    # The sdist already supplies Abseil. Upstream FetchContent otherwise deletes
    # that pinned copy and performs a floating Git/tag download at configure time.
    # Restore exactly the archived input if a previous upstream configure removed it.
    absl=spm/'third_party/abseil-cpp'
    if not (absl/'CMakeLists.txt').is_file():
        item=next(x for x in lock['sources'] if x['name']=='sentencepiece')
        archive=fetch(item,args.cache)
        with tarfile.open(archive) as tar:
            prefix='sentencepiece-0.2.2/sentencepiece/third_party/abseil-cpp/'
            for member in tar:
                if not member.name.startswith(prefix) or not member.isfile():continue
                target=(absl/member.name[len(prefix):]).resolve()
                if not target.is_relative_to(absl.resolve()):raise ValueError('Unsafe bundled Abseil path')
                target.parent.mkdir(parents=True,exist_ok=True)
                with tar.extractfile(member) as src,target.open('wb') as out:shutil.copyfileobj(src,out)
    cmake_file=spm/'CMakeLists.txt';text=cmake_file.read_text(encoding='utf8');previous=text
    text,count=re.subn(r'  FetchContent_Populate\(abseil-cpp\s+GIT_REPOSITORY.*?GIT_TAG 20260526\.0\)',
        '  # BianLingo: use the checksum-pinned Abseil source supplied by this sdist.',text,flags=re.S)
    if count!=1 and 'BianLingo: use the checksum-pinned Abseil' not in text:raise ValueError('SentencePiece dependency patch no longer matches')
    if text!=previous:cmake_file.write_text(text,encoding='utf8')
    # Do not require Windows Developer Mode or symbolic-link privileges.
    if not (spm/'third_party/absl').exists():shutil.copytree(absl/'absl',spm/'third_party/absl')
    # Chat is bursty; Ruy's GEMM-oriented 2ms spin repeatedly burns CPU between
    # decoder operations. Use its existing passive-wait path for this worker.
    pool=source/'ctranslate2/third_party/ruy/ruy/thread_pool.h'
    pool_text=pool.read_text(encoding='utf8')
    before='Duration spin_duration_ = DurationFromMilliseconds(2);'
    after='Duration spin_duration_ = DurationFromMilliseconds(0); // BianLingo: passive chat worker'
    if before not in pool_text and after not in pool_text:raise ValueError('Ruy passive-wait patch no longer matches')
    if before in pool_text:pool.write_text(pool_text.replace(before,after),encoding='utf8')
    command=[args.cmake,'-S',str(ROOT/'tools/ctranslate2_native'),'-B',str(args.build),'-A','x64',
             '-DCT2_SOURCE='+str(source/'ctranslate2'),'-DSPM_SOURCE='+str(spm)]
    subprocess.run(command,check=True)
    subprocess.run([args.cmake,'--build',str(args.build),'--config','Release','--target','BianLingoCT2','--parallel',str(args.jobs)],check=True)
    args.output.mkdir(parents=True,exist_ok=True);binary=args.output/'BianLingoCT2.exe'
    shutil.copy2(args.build/'Release/BianLingoCT2.exe',binary)
    subprocess.run([str(binary.resolve()),'--self-test'],check=True)
    dumpbin=args.dumpbin or shutil.which('dumpbin')
    if not dumpbin:
        vswhere=Path(os.environ.get('ProgramFiles(x86)','C:/Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
        if vswhere.exists():
            install=subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip()
            candidates=sorted(Path(install).glob('VC/Tools/MSVC/*/bin/Hostx64/x64/dumpbin.exe')) if install else []
            if candidates:dumpbin=str(candidates[-1])
    if not dumpbin:raise ValueError('MSVC dumpbin is required for the independent dependency check')
    dependencies=subprocess.check_output([dumpbin,'/dependents',str(binary)],text=True,errors='replace')
    imports=sorted(set(re.findall(r'^\s+([A-Za-z0-9_.-]+\.dll)\s*$',dependencies,re.M|re.I)))
    # A small import allowlist is stronger than merely rejecting known GPU DLLs.
    if not imports or any(n.lower() not in ('kernel32.dll','advapi32.dll','shell32.dll','bcrypt.dll','dbghelp.dll') for n in imports):
        raise ValueError('Native worker contains an unexpected external runtime: '+str(imports))
    notices=['BianLingoCT2 CPU-only worker\nProject wrapper: MIT. Libraries keep their own terms.\n'
             'Pinned source/build inputs follow; a source-tree notice is not a claim every test/library is linked.\n']
    records=[]
    for item in lock['sources']:
        base=spm if item['name']=='sentencepiece' else source/'ctranslate2'/item['destination']
        found=[]
        for p in sorted(base.rglob('*')):
            if not p.is_file():continue
            rel=p.relative_to(base)
            # Submodule notices are recorded under their own pinned input.
            if item['name']=='ctranslate2' and rel.parts[0]=='third_party':continue
            if item['name']=='ruy' and rel.parts[0]=='third_party':continue
            if any(k in p.name.upper() for k in ('LICENSE','COPYING','NOTICE','COPYRIGHT','THIRD-PARTY-PROGRAM')):
                raw=p.read_bytes()
                try:text=raw.decode('utf8')
                except UnicodeDecodeError:text=raw.decode('cp1252')
                found.append(dict(path=rel.as_posix(),sha256=hashlib.sha256(raw).hexdigest()))
                notices.append('\n===== '+item['name']+'/'+rel.as_posix()+' =====\n'+text)
        if not found:raise ValueError('Missing source notices: '+item['name'])
        records.append(dict(source=item,notices=found))
    # CT2 vendors nlohmann/json 3.11.2 as a single header without a separate license.
    notices.append('\n===== nlohmann/json 3.11.2 =====\n'+(ROOT/'resources/licenses/nlohmann-json-3.11.2.txt').read_text(encoding='utf8'))
    notices.append('\n===== BS thread pool 5.1.0 MIT =====\n'+(ROOT/'resources/licenses/BS_thread_pool.txt').read_text(encoding='utf8'))
    # These standalone upstream headers carry additional math/thread-pool
    # attribution not covered by CT2's top-level MIT notice. Preserve them whole.
    for p in sorted((source/'ctranslate2/third_party').iterdir()):
        if p.is_file() and p.suffix in ('.h','.hpp'):
            notices.append('\n===== CTranslate2 bundled header '+p.name+' =====\n'+p.read_text(encoding='utf8'))
    notices.append('\n===== SIMD_Utils BSD-2-Clause =====\n'+(ROOT/'resources/licenses/simd_utils.txt').read_text(encoding='utf8'))
    notices.append('\n===== Project MIT =====\n'+(ROOT/'LICENSE').read_text(encoding='utf8'))
    (args.output/'engine-LICENSES.txt').write_text('\n'.join(notices),encoding='utf8')
    sha=digest(binary);(args.output/'engine.sha256').write_text(sha+'\n',encoding='ascii')
    (args.output/'native-build.json').write_text(json.dumps(dict(schemaVersion=1,protocol='BLCT3',
        version=lock['ctranslate2'],backend=lock['backend'],binary_sha256=sha,imports=imports,
        source_lock_sha256=fingerprint,inputs=records,
        source_patches=['Use the sdist-vendored Abseil; disable configure-time fetch; copy headers instead of symlinks',
                        'Ruy thread_pool.h: change default 2ms spin to 0ms passive wait for bursty chat'],
        wrapper_sources={p.relative_to(ROOT).as_posix():hashlib.sha256(p.read_text(encoding='utf8').encode('utf8')).hexdigest() for p in
                         [ROOT/'tools/build_ct2_native.py',*sorted((ROOT/'tools/ctranslate2_native').iterdir())] if p.is_file()}),indent=2)+'\n',encoding='utf8')
    print('Built verified CPU-only worker:',binary)

if __name__=='__main__':main()
