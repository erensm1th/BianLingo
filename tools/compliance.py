"""Offline release evidence checks. Technical validation is not legal clearance."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import posixpath
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]

def load(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda:f.read(1024*1024),b''): h.update(chunk)
    return h.hexdigest()

def terminology_policy(root=ROOT):
    unresolved=load(root/'resources/data/glossary-provenance.json')['unresolved_entries']
    if not unresolved: return
    policy=next((p for p in load(root/'resources/release-review.json').get('deferred',[])
                 if p.get('id')=='terminology-unverified'),None)
    signature=hashlib.sha256(json.dumps(unresolved,ensure_ascii=False,sort_keys=True,
                                      separators=(',',':')).encode('utf8')).hexdigest()
    if not policy or policy.get('decision')!='maintainer-deferred' or policy.get('clearance_claim') is not False or \
       policy.get('unresolved_count')!=len(unresolved) or policy.get('unresolved_sha256')!=signature:
        raise ValueError('Terminology source review or an exact, documented maintainer deferral is required')

def validate_model_evidence(root=ROOT):
    records={m['pair']:m for m in load(root/'resources/model-evidence/ctranslate2/index.json')['models']}
    for model in load(root/'resources/ctranslate2-models.json')['models']:
        item=records.get(model['pair'])
        if not item or item.get('review_status')!='reviewed-license-notices' or \
           model.get('provenance_status')!='reviewed-publisher-declared-lineage' or \
           item.get('assurance')!='publisher-declared-lineage' or not model.get('license_resolution'):
            raise ValueError('Reviewed model evidence missing: '+model['pair'])
        # A source-card revision is evidence of its terms, not proof of the
        # original checkpoint used by a third-party converter.
        if item['original_weight_revision']!=model.get('original_weight_revision'):
            raise ValueError('Unrecorded original checkpoint was relabeled: '+model['pair'])
        for kind,repository,revision in [('upstream',model['source_model'],model['license_evidence_revision']),
                                        ('conversion',model['repository'],model['revision'])]:
            card=item['cards'][kind]
            expected=f'resources/model-evidence/ctranslate2/{model["pair"]}/{kind}-README.md'
            if card['path']!=expected or card['repository']!=repository or card['revision']!=revision or \
               digest(root/expected)!=card['sha256']:
                raise ValueError('Pinned model evidence changed: '+model['pair'])
            text=(root/expected).read_text(encoding='utf8')
            license_id=model['original_license'] if kind=='upstream' else model['converted_license']
            if not re.search(r'^license:\s*'+re.escape(license_id)+r'\s*$',text,re.M|re.I):
                raise ValueError('Model card license differs from attribution: '+model['pair'])
            if kind=='conversion' and (model['source_model'] not in text or
                    card['sha256']!=model['files']['README.md'][1]):
                raise ValueError('Conversion/source evidence differs: '+model['pair'])


def validate_runtime_evidence(root=ROOT):
    runtime=load(root/'resources/ctranslate2-runtime.json')
    lock=load(root/'resources/ct2-native-sources.json')
    signature=hashlib.sha256(json.dumps(lock,sort_keys=True).encode()).hexdigest()
    notices=load(root/'resources/ct2-native-notices.json')
    if runtime.get('kind')!='native-cpu' or runtime.get('artifacts')!=[] or \
       runtime.get('source_lock_sha256')!=signature or notices.get('source_lock_sha256')!=signature or \
       notices.get('review_status')!='reviewed-source-notices' or \
       digest(root/'resources/licenses/CT2Native.txt')!=notices.get('notice_sha256'):
        raise ValueError('Native runtime source/notice review missing or changed')
    if len(lock['sources'])!=6 or any(not re.fullmatch('[a-f0-9]{64}',s['sha256']) or s['size']<=0 for s in lock['sources']):
        raise ValueError('Native runtime inputs are not fixed')
    review=load(root/'resources/ollama-runtime-review.json')
    directory=root/'resources/model-evidence/ollama'
    audit=load(directory/'archive-audit.json');smoke=load(directory/'cpu-smoke.json')
    if review.get('review_status')!='reviewed-publisher-direct-inventory' or \
       review.get('distribution')!='download-directly-from-publisher' or \
       audit['archive_sha256']!='7afa38624fa62ee5d8293422d0ab084c1bb4603134c7f4dafce11a033b57cc19' or \
       audit['archive_size']!=1601969847 or audit['version']!='v0.40.13' or \
       digest(directory/'archive-audit.json')!=review['inventory_sha256'] or \
       digest(directory/'cpu-smoke.json')!=review['cpu_smoke_sha256'] or \
       digest(root/'resources/licenses/OllamaRuntime.txt')!=review['notice_sha256']:
        raise ValueError('Optional publisher-direct runtime evidence changed')
    records={m['path']:m for m in audit['members']}
    if len(records)!=75 or not all(re.fullmatch('[a-f0-9]{64}',m['sha256']) for m in records.values()):
        raise ValueError('Optional runtime inventory is incomplete')
    for name in ('GO_LICENSE','CPP_HTTPLIB_LICENSE','LLAMA_CPP_LICENSE','LLAMA_CPP_VENDORS_LICENSE'):
        path='lib/ollama/'+name
        if digest(directory/path)!=records[path]['sha256']:
            raise ValueError('Original publisher notice changed')
    if smoke['version']['version']!='0.40.13' or not smoke['result'].get('response') or not smoke.get('model_license'):
        raise ValueError('Actual optional runtime smoke/license evidence missing')

def validate_sources(root=ROOT):
    licenses=root/'resources/licenses'
    for item in load(licenses/'sources.json'):
        if digest(licenses/item['file']) != item['sha256']: raise ValueError('Pinned notice changed: '+item['file'])
    provenance=load(root/'resources/data/glossary-provenance.json')
    glossary=load(root/'resources/data/glossary_zhCN.json')
    from refresh_glossary_provenance import PATTERN, string
    mappings={}
    if glossary['terms'].keys()!=provenance['entries'].keys(): raise ValueError('Glossary provenance coverage differs')
    for item in provenance['files']:
        snapshot=root/'resources/data/provenance-source'/item['path']
        if digest(snapshot)!=item['sha256']: raise ValueError('Terminology evidence changed')
        mappings[item['path']]={(string(k).lower(),string(v)) for k,v in PATTERN.findall(snapshot.read_text(encoding='utf-8-sig'))}
    for key,origins in provenance['entries'].items():
        if any((key,glossary['terms'][key]) not in mappings.get(origin,set()) for origin in origins):
            raise ValueError('Glossary entry does not match its claimed source: '+key)
        if not origins and provenance['unresolved_entries'].get(key)!=glossary['terms'][key]:
            raise ValueError('Unreviewed glossary entry changed without provenance record')
    models=load(root/'resources/ctranslate2-models.json')
    for m in models['models']:
        if m['license'] not in ('Apache-2.0','CC-BY-4.0') or m['license']!=m['original_license']:
            raise ValueError('Unsupported or inconsistent model license: '+m['pair'])
        if not re.fullmatch('[a-f0-9]{40}',m['license_evidence_revision']): raise ValueError('Model license evidence is not pinned')
        if not m['attribution'] or not m['changes'] or not m['provenance_status']: raise ValueError('Model attribution missing')
        if not re.fullmatch('[a-f0-9]{40}',m['revision']): raise ValueError('Conversion is not pinned')
    validate_model_evidence(root)
    validate_runtime_evidence(root)
    runtime=load(root/'resources/ctranslate2-runtime.json')
    for a in runtime['artifacts']:
        if not a.get('primary_license') or not a.get('source') or not re.fullmatch('[a-f0-9]{64}',a['sha256']):
            raise ValueError('Runtime provenance is incomplete')
    branding=load(root/'resources/branding/manifest.json')
    if branding['source']!='resources/branding/logo.png' or digest(root/branding['source'])!=branding['source_sha256']:
        raise ValueError('Original brand artwork changed without review')
    if set(branding['derivatives'])!={'resources/bianlingo.ico','resources/settings-icon.ico','addon/BianLingo/Textures/Logo.tga'}:
        raise ValueError('Brand icon/texture inventory is incomplete')
    if branding.get('settings_source') != 'resources/branding/settings-icon.png' or digest(root/branding['settings_source']) != branding.get('settings_source_sha256'):
        raise ValueError('Pixel settings artwork changed without review')
    for name,expected in branding['derivatives'].items():
        if digest(root/name)!=expected:raise ValueError('Brand derivative changed: '+name)

def validate_source_archive(stage):
    runtime=stage/'data/bergamot';info=load(runtime/'source-manifest.json')
    if info['filename']!='corresponding-source.zip' or digest(runtime/info['filename'])!=info['sha256']:
        raise ValueError('Missing or changed corresponding source archive')
    if digest(runtime/'BianLingoBergamot.exe')!=info['binary_sha256']: raise ValueError('Source archive belongs to a different helper')
    with ZipFile(runtime/info['filename']) as archive:
        records=json.loads(archive.read('SOURCE_FILES.json'))
        if records['binary_sha256']!=info['binary_sha256']: raise ValueError('Source/binary manifest mismatch')
        if len(archive.namelist())!=len(set(archive.namelist())): raise ValueError('Duplicate source entries')
        if set(archive.namelist())!={x['path'] for x in records['files']}|{'README.txt','SOURCE_FILES.json'}:
            raise ValueError('Unlisted source archive entries')
        for item in records['files']:
            name=item['path'];path=Path(name)
            if path.is_absolute() or '\\' in name or ':' in name or '..' in path.parts or any(p in ('.git','UserData','__pycache__') for p in path.parts):
                raise ValueError('Unsafe source archive member')
            data=archive.read(name)
            if len(data)!=item['size'] or hashlib.sha256(data).hexdigest()!=item['sha256']:
                raise ValueError('Corresponding source content mismatch')
        required={'bergamot-source/LICENSE','project/tools/bergamot_helper/src/main.cpp',
            'project/tools/build_bergamot.py','project/tools/bergamot_helper/CMakeLists.txt',
            'bergamot-source/3rd_party/marian-dev/src/common/git_revision.h',
            'bergamot-source/3rd_party/ssplit-cpp/src/3rd-party/pcre2-10.39/LICENCE'}
        if not required<=set(archive.namelist()): raise ValueError('Corresponding source is incomplete')
        for link in records.get('materialized_links',[]):
            name=link['path'];target=link['target']
            resolved=posixpath.normpath(posixpath.join(posixpath.dirname(name),target))
            if not name.startswith('bergamot-source/') or target.startswith('/') or '\\' in target or ':' in target or \
               not resolved.startswith('bergamot-source/') or name not in archive.namelist() or resolved not in archive.namelist() or \
               archive.read(name)!=archive.read(resolved):
                raise ValueError('Invalid materialized source link')

def validate_native_notices(prefix, json_prefix=None):
    simple={'openssl':'OpenSSL','nlohmann-json':'nlohmann-json','tesseract':'Tesseract','leptonica':'Leptonica'}
    sections=re.split(r'^===== ([a-z0-9-]+) =====\s*\n', (ROOT/'resources/licenses/OCRDependencies.txt').read_text(encoding='utf8'),flags=re.M)
    expected={name:body.strip() for name,body in zip(sections[1::2],sections[2::2])}
    expected.update({k:(ROOT/f'resources/licenses/{v}.txt').read_text(encoding='utf8').strip() for k,v in simple.items()})
    for name,notice in expected.items():
        actual=((json_prefix if name=='nlohmann-json' and json_prefix else prefix)/'share'/name/'copyright').read_text(encoding='utf8').strip()
        if actual!=notice: raise ValueError('Embedded notice differs from installed dependency: '+name)
    for package in load(ROOT/'docs/audits/2026-11-03-native-dependencies.json')['packages']:
        name=package['name']; actual_prefix=json_prefix if name=='nlohmann-json' and json_prefix else prefix
        record=load(actual_prefix/'share'/name/'vcpkg.spdx.json')['packages'][0]
        if record.get('versionInfo','').split('#')[0]!=package['version'].split('#')[0]:
            raise ValueError('Build-input version changed; refresh inventory and license audit: '+name)

def formal_gate(root=ROOT):
    review=load(root/'resources/release-review.json')
    if review.get('releaseStatus')!='approved' or review.get('pending'):
        raise ValueError('Formal release blocked: '+', '.join(x['id'] for x in review.get('pending',[])))
    validate_sources(root)
    if load(root/'resources/ctranslate2-runtime.json')['review_status']!='reviewed-native-source-build': raise ValueError('Runtime review missing')
    terminology_policy(root)
    from export_public_snapshot import source_digest
    if review.get('source_review',{}).get('eligible_source_sha256')!=source_digest(root):
        raise ValueError('Public source changed after candidate review')

def inventory(stage):
    # A scoped, evidence-based inventory; build inputs are not mislabeled as proven linked code.
    return dict(schemaVersion=1, kind='distribution-and-optional-download-inventory',
        limitations='Not a binary object reachability proof or legal clearance. See release-review.json.',
        files=[dict(path=p.relative_to(stage).as_posix(),size=p.stat().st_size,sha256=digest(p))
               for p in sorted(stage.rglob('*')) if p.is_file() and p.relative_to(stage).as_posix() not in ('artifact-inventory.json','native-build.json')],
        nativeBuildInputs=load(ROOT/'docs/audits/2026-11-03-native-dependencies.json')['packages'],
        optionalCTranslate2Models=load(ROOT/'resources/ctranslate2-models.json')['models'],
        optionalCTranslate2Runtime=load(ROOT/'resources/ctranslate2-runtime.json'),
        bergamotModels=load(ROOT/'resources/bergamot-models.json'),
        ocrModels=load(ROOT/'resources/ocr-models.json'),
        terminologyEvidence={k:v for k,v in load(ROOT/'resources/data/glossary-provenance.json').items()
                              if k not in ('entries','unresolved_entries')},
        review=load(ROOT/'resources/release-review.json'))

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--stage',type=Path);parser.add_argument('--vcpkg',type=Path)
    parser.add_argument('--json-prefix',type=Path,help='Actual nlohmann/json dependency prefix when separately resolved')
    parser.add_argument('--formal',action='store_true');args=parser.parse_args()
    validate_sources()
    if args.vcpkg: validate_native_notices(args.vcpkg,args.json_prefix)
    if args.stage: validate_source_archive(args.stage)
    if args.formal: formal_gate()
    print('PASS technical license/provenance checks; formal clearance is a separate gate')
