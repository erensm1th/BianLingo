"""Pin bilingual OPUS-MT conversions and portable Windows runtime artifacts.

Downloads metadata and small model support files only, never model weights.
The desktop app consumes the checked-in snapshot without live catalogue queries.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[1]
LANGUAGES = 'zh fr de es it pt ru ja ko ar nl pl tr uk cs da el fi hu no ro sv vi id th'.split()
FILES = {'model.bin', 'config.json', 'shared_vocabulary.json', 'source.spm', 'target.spm', 'README.md'}

def get(url):
    with urlopen(url, timeout=40) as response:
        return response.read()

def model(pair):
    repository = f'ooeoeo/opus-mt-{pair}-ct2-float16'
    try:
        record = json.loads(get(f'https://huggingface.co/api/models/{repository}?blobs=true'))
    except HTTPError as error:
        # Hub deliberately uses 401 for repositories that are absent or private.
        # Neither belongs in the public, anonymous-download catalogue.
        if error.code in (401, 404): return None
        raise
    card = record.get('cardData', {})
    if card.get('license') != 'apache-2.0' or card.get('source_model') != f'Helsinki-NLP/opus-mt-{pair}':
        return None
    siblings = {r['rfilename']: r for r in record['siblings']}
    if not FILES <= siblings.keys(): return None
    files = {}
    for name in sorted(FILES):
        r = siblings[name]
        if 'lfs' in r:
            size, digest = r['lfs']['size'], r['lfs']['sha256']
        else:
            data = get(f'https://huggingface.co/{repository}/resolve/{record["sha"]}/{name}')
            size, digest = len(data), hashlib.sha256(data).hexdigest()
        files[name] = [size, digest]
    original = json.loads(get(f'https://huggingface.co/api/models/{card["source_model"]}'))
    original_license = original.get('cardData', {}).get('license')
    licenses = {'apache-2.0':'Apache-2.0', 'cc-by-4.0':'CC-BY-4.0'}
    if original_license not in licenses:
        raise ValueError(f'Unreviewed original license: {pair}: {original_license}')
    source, target = pair.split('-')
    return dict(pair=pair, source='zh-Hans' if source == 'zh' else source,
                target='zh-Hans' if target == 'zh' else target, repository=repository,
                revision=record['sha'], source_model=card['source_model'], license=licenses[original_license],
                original_license=licenses[original_license], converted_license='Apache-2.0',
                original_weight_revision=None, license_evidence_revision=original['sha'],
                attribution=f'Helsinki-NLP / OPUS-MT contributors; conversion: {repository}',
                changes='Upstream OPUS-MT converted to CTranslate2 float16 by the named conversion publisher. Project pins and distributes the catalog; no claim of original model authorship.',
                provenance_status='collected-not-approved',
                files=files)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--languages', nargs='+', default=LANGUAGES)
    args = parser.parse_args()
    pairs = sorted({f'{language}-en' for language in args.languages} | {f'en-{language}' for language in args.languages})
    with ThreadPoolExecutor(max_workers=6) as pool:
        models = [r for r in pool.map(model, pairs) if r]
    # Keep the pinned Chinese model revisions byte-compatible.
    old = json.loads((ROOT/'resources/ctranslate2-models.json').read_text(encoding='utf8'))
    for r in models:
        prior = next((m for m in old['models'] if m['pair'] == r['pair']), None)
        if prior:
            if prior['revision'] != r['revision']: raise ValueError('Pinned model revision changed')
            for name, expected in prior['files'].items():
                if r['files'].get(name) != expected: raise ValueError('Pinned file changed')
            # Carry an existing manual review only when all terms/evidence and
            # model bytes are identical. New inputs require fresh pinned cards.
            keys=('source_model','original_license','converted_license','license_evidence_revision','files','revision')
            if all(prior.get(key)==r.get(key) for key in keys):
                r.update(provenance_status=prior['provenance_status'],
                         license_resolution=prior.get('license_resolution',''))
    candidate=dict(schemaVersion=3,models=models)
    output=ROOT/'resources/ctranslate2-models.candidate.json'
    output.write_text(json.dumps(candidate,indent=2)+'\n',encoding='utf8')
    print(f'Wrote {len(models)} directions to {output.name}; review evidence before replacing the catalog. Native runtime lock unchanged.')

if __name__ == '__main__': main()
