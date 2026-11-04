"""Assemble native CT2 and explicitly selected weights; no pip/Python runtime."""
import argparse
import json
from pathlib import Path
import shutil
from urllib.request import urlopen
from zipfile import ZIP_DEFLATED, ZipFile
from ct2_runtime import FILES, sha, validate_runtime
ROOT=Path(__file__).resolve().parents[1]

def download(url,target,expected,size):
    if target.is_file() and target.stat().st_size==size and sha(target)==expected:return
    target.parent.mkdir(parents=True,exist_ok=True);temporary=target.with_suffix(target.suffix+'.download')
    try:
        with urlopen(url,timeout=120) as source,temporary.open('wb') as out:
            while chunk:=source.read(1024*1024):out.write(chunk)
        if temporary.stat().st_size!=size or sha(temporary)!=expected:raise ValueError('Model hash mismatch')
        temporary.replace(target)
    finally:temporary.unlink(missing_ok=True)

def main():
    p=argparse.ArgumentParser();p.add_argument('--runtime',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--pairs',nargs='*',default=['en-zh','zh-en'])
    p.add_argument('--zip',type=Path);args=p.parse_args();validate_runtime(args.runtime)
    models=json.loads((ROOT/'resources/ctranslate2-models.json').read_text(encoding='utf8'));by_id={m['pair']:m for m in models['models']}
    if any(pair not in by_id for pair in args.pairs):raise ValueError('Unknown pair')
    destination=args.output/'data/ctranslate2'
    if destination.exists():raise FileExistsError('Use an empty component output directory')
    destination.mkdir(parents=True)
    for name in FILES:shutil.copy2(args.runtime/name,destination/name)
    for pair in dict.fromkeys(args.pairs):
        m=by_id[pair];directory=destination/'models'/pair
        for name,(size,digest) in m['files'].items():
            download(f'https://huggingface.co/{m["repository"]}/resolve/{m["revision"]}/{name}',directory/name,digest,size)
        metadata={k:v for k,v in m.items() if k!='files'}
        (directory/'ATTRIBUTION.json').write_text(json.dumps(metadata,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
        original='CC-BY-4.0.txt' if m['original_license']=='CC-BY-4.0' else 'SentencePiece.txt'
        shutil.copy2(ROOT/'resources/licenses'/original,directory/'LICENSE-Upstream.txt')
        shutil.copy2(ROOT/'resources/licenses/SentencePiece.txt',directory/'LICENSE-Conversion.txt')
        (directory/'PROVENANCE.txt').write_text(m['attribution']+'\n'+m['changes']+'\n'+m['license_resolution']+
            '\nhttps://huggingface.co/'+m['source_model']+'/tree/'+m['license_evidence_revision']+
            '\nCard revision is license evidence; the original weight checkpoint is unrecorded.\n',encoding='utf8')
    shutil.copy2(ROOT/'resources/ctranslate2-models.json',destination/'models.json')
    if args.zip:
        args.zip.parent.mkdir(parents=True,exist_ok=True)
        with ZipFile(args.zip,'x',compression=ZIP_DEFLATED) as archive:
            for path in sorted(destination.rglob('*')):
                if path.is_file():archive.write(path,path.relative_to(args.output))
if __name__=='__main__':main()
