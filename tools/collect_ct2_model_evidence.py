"""Freeze pinned publisher cards; collecting evidence does not approve a model.

The publisher names an upstream model but does not record the original weight
revision. Keep that distinction; never fill it with the current upstream SHA.
"""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import re
import time
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]

def fetch(url):
    for attempt in range(3):
        try:
            with urlopen(Request(url, headers={'User-Agent':'BianLingo-Provenance'}), timeout=25) as response:
                data=response.read(256*1024+1)
            if len(data)>256*1024: raise ValueError('Model card is too large')
            return data
        except Exception:
            if attempt==2: raise
            time.sleep(attempt+1)

def collect(model):
    pair=model['pair']; result=dict(pair=pair, assurance='publisher-declared-lineage',
        original_weight_revision=None, review_status='collected-not-approved', cards={})
    for kind,repository,revision in [('upstream',model['source_model'],model['license_evidence_revision']),
                                     ('conversion',model['repository'],model['revision'])]:
        if not re.fullmatch('[a-f0-9]{40}',revision): raise ValueError('Unpinned source')
        url=f'https://huggingface.co/{repository}/raw/{revision}/README.md'
        data=fetch(url)
        if kind=='conversion' and hashlib.sha256(data).hexdigest()!=model['files']['README.md'][1]:
            raise ValueError('Conversion card differs from pinned model file')
        relative=Path('resources/model-evidence/ctranslate2')/pair/(kind+'-README.md')
        destination=ROOT/relative;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data)
        result['cards'][kind]=dict(repository=repository, revision=revision, path=relative.as_posix(),
                                  url=url, sha256=hashlib.sha256(data).hexdigest())
    return result

def main():
    models=json.loads((ROOT/'resources/ctranslate2-models.json').read_text(encoding='utf8'))['models']
    with ThreadPoolExecutor(max_workers=6) as executor: records=list(executor.map(collect,models))
    output=ROOT/'resources/model-evidence/ctranslate2/index.json'
    output.write_text(json.dumps(dict(schemaVersion=1, scope='Pinned model cards, not reproduction of original weights',
                                     models=records),ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(f'Collected {len(records)} pinned card pairs; no approval status was changed')

if __name__=='__main__':main()
