"""Audit the complete fixed publisher ZIP; emit evidence, never a release."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from urllib.request import urlopen
from zipfile import ZipFile,ZIP_DEFLATED
import os
import time
import urllib.error

VERSION='v0.40.13'
URL=f'https://github.com/ollama/ollama/releases/download/{VERSION}/ollama-windows-amd64.zip'
SHA='7afa38624fa62ee5d8293422d0ab084c1bb4603134c7f4dafce11a033b57cc19'
SIZE=1601969847

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        while b:=f.read(1024*1024):h.update(b)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser();p.add_argument('--root',type=Path,required=True)
    p.add_argument('--smoke',action='store_true');args=p.parse_args()
    root=args.root.resolve();root.mkdir(parents=True,exist_ok=True);archive=root/'ollama.zip'
    if not archive.exists() or digest(archive)!=SHA:
        with urlopen(URL,timeout=120) as source,archive.open('wb') as out:shutil.copyfileobj(source,out,1024*1024)
    if archive.stat().st_size!=SIZE or digest(archive)!=SHA:raise ValueError('Official ZIP SHA mismatch')
    evidence=root/'evidence';evidence.mkdir(exist_ok=True);members=[]
    cpu=root/'cpu-smoke';cpu.mkdir(exist_ok=True)
    with ZipFile(archive) as z:
        if len(set(z.namelist()))!=len(z.namelist()):raise ValueError('Duplicate ZIP names')
        for item in z.infolist():
            path=Path(item.filename)
            if path.is_absolute() or '..' in path.parts or '\\' in item.filename or ':' in item.filename:raise ValueError('Unsafe ZIP')
            if item.is_dir():continue
            data=z.read(item);members.append(dict(path=item.filename,size=len(data),sha256=hashlib.sha256(data).hexdigest()))
            if 'LICENSE' in path.name.upper():
                target=evidence/'notices'/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
            if item.filename=='ollama.exe' or item.filename.startswith('lib/ollama/') and len(path.parts)==3:
                target=cpu/path;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
    (evidence/'archive-audit.json').write_text(json.dumps(dict(schemaVersion=1,version=VERSION,url=URL,
        archive_sha256=SHA,archive_size=SIZE,members=members,
        scope='Complete ZIP content inventory. Hashes establish integrity, not legal clearance.'),indent=2)+'\n',encoding='utf8')
    if args.smoke:
        env=os.environ.copy();home=root/'user';home.mkdir(exist_ok=True)
        for name in ('USERPROFILE','LOCALAPPDATA','APPDATA','TEMP','TMP'):env[name]=str(home)
        env.update(OLLAMA_HOST='127.0.0.1:11437',OLLAMA_MODELS=str(home/'models'),OLLAMA_NO_CLOUD='1')
        log=(root/'server.log').open('wb')
        server=subprocess.Popen([str(cpu/'ollama.exe'),'serve'],cwd=cpu,env=env,stdout=log,stderr=log)
        def api(path,body=None,timeout=30):
            import urllib.request
            request=urllib.request.Request('http://127.0.0.1:11437/api/'+path,
                data=json.dumps(body).encode() if body else None,headers={'Content-Type':'application/json'})
            with urllib.request.urlopen(request,timeout=timeout) as response:return json.load(response)
        try:
            for i in range(45):
                if server.poll() is not None:raise ValueError('Official CPU server failed to start')
                try:version=api('version');break
                except (OSError,urllib.error.URLError):time.sleep(1)
            else:raise TimeoutError('Server startup')
            subprocess.run([str(cpu/'ollama.exe'),'pull','qwen3:0.6b'],env=env,cwd=cpu,check=True,timeout=420)
            result=api('generate',dict(model='qwen3:0.6b',prompt='Translate this World of Warcraft chat into Simplified Chinese. Output only the translation: Looking for a healer for Deadmines.',
                stream=False,think=False,options=dict(num_gpu=0,num_predict=80,temperature=0)),timeout=180)
            if not result.get('response','').strip():raise ValueError('Official runtime returned no translation')
            shown=api('show',dict(model='qwen3:0.6b'))
            (evidence/'cpu-smoke.json').write_text(json.dumps(dict(version=version,model='qwen3:0.6b',
                result=result,model_license=shown.get('license'),tags=api('tags'),scope='GitHub Windows runner, CPU, synthetic text; not user gaming/HDR performance'),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        finally:
            subprocess.run(['taskkill','/PID',str(server.pid),'/T','/F'],capture_output=True)
            server.wait(timeout=15);log.close()
    print(f'PASS complete official archive SHA256, {len(members)} files; no release published')

if __name__=='__main__':main()
