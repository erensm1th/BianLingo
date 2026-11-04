"""Freeze attribution evidence for an existing glossary, without executing upstream Lua.

Unknown/modified entries are recorded honestly, not reclassified as upstream data.
Input must be a local checkout; this tool never silently advances a source version.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
FILES = ['LICENSE', 'Locales/zhCN.lua',
    'Libs/LibBabble-3.0/LibBabble-3.0.lua',
    'Libs/LibBabble-Race-3.0/LibBabble-Race-3.0.lua',
    'Libs/LibBabble-ItemSet-3.0/LibBabble-ItemSet-3.0.lua',
    'Libs/LibBabble-SubZone-3.0/LibBabble-SubZone-3.0.lua',
    'Libs/LibBabble-SubZone-3.0/LibBabble-SubZone-3.0_zhCN.lua']
PATTERN = re.compile(r'\["((?:[^"\\]|\\.)*)"\]\s*=\s*"((?:[^"\\]|\\.)*)"')

def string(value):
    # Deliberately support only the quoted-string subset present in these tables.
    return re.sub(r'\\([\\"\'])', r'\1', value)

def freeze(source):
    revision = subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'], text=True).strip()
    records, matches, headers = [], {}, []
    for name in FILES:
        # Hash the exact committed blob, independent of Windows checkout newline conversion.
        data = subprocess.check_output(['git','-C',str(source),'show',f'{revision}:{name}'])
        text = data.decode('utf-8-sig')
        snapshot = ROOT/'resources/data/provenance-source'/name
        snapshot.parent.mkdir(parents=True,exist_ok=True)
        snapshot.write_bytes(data)
        license_id = 'LicenseRef-Public-Domain' if 'LibBabble-3.0/LibBabble-3.0.lua' in name else 'MIT'
        records.append(dict(path=name, sha256=hashlib.sha256(data).hexdigest(), license=license_id,
            url=f'https://github.com/Pirson-s-Addons/WoW-Translator/blob/{revision}/{name}'))
        # Retain the exact supplied license/header, not an invented copyright year.
        header = text if name == 'LICENSE' else re.search(r'--\[\[(.*?)\]\]', text, re.S)
        headers.append(f'===== {name} =====\n' + (header if isinstance(header,str) else header.group(0) if header else
            'WoWTranslator locale data; repository MIT license above.') + '\n')
        for key, value in PATTERN.findall(text):
            key, value = string(key).lower(), string(value)
            matches.setdefault((key,value), []).append(name)
    glossary_path = ROOT/'resources/data/glossary_zhCN.json'
    glossary = json.loads(glossary_path.read_text(encoding='utf8'))
    lineage = {key: matches.get((key,value), []) for key,value in glossary['terms'].items()}
    unknown = {key: glossary['terms'][key] for key, origins in lineage.items() if not origins}
    lock = dict(schemaVersion=1, repository='Pirson-s-Addons/WoW-Translator', revision=revision,
        basis='2026-11-03 evidence snapshot; not a claim that this was the historical import revision.',
        changes='Case-normalized keys; merged tables; project/user overrides. No upstream Lua code is executed.',
        files=records, entries=lineage, unresolved_entries=unknown,
        unresolved_status='Historical overrides / normalization / user mappings; exact import lineage pending review.')
    (ROOT/'resources/data/glossary-provenance.json').write_text(json.dumps(lock,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    notice = ('Terminology data provenance\n\nWoWTranslator: Pirson, MIT.\n'
        'LibBabble tables have separate maintainers and licenses as retained below.\n'
        'MIT permission and warranty text applies as declared in each MIT table header.\n'
        'The generic LibBabble core declares Public Domain; this does not relicense other tables.\n'
        'Full per-entry evidence: resources/data/glossary-provenance.json in project source.\n'
        'Historical unmatched entries remain pending review; no article prose is intentionally bundled.\n\n')
    evidence = f'Frozen evidence revision: {revision}\n' + '\n'.join(
        f'{item["url"]}\nSHA-256: {item["sha256"]}\n' for item in records)
    (ROOT/'resources/data/licenses/Terminology.txt').write_text(notice+evidence+'\n'+'\n'.join(headers),encoding='utf8')
    glossary['license'] = 'Mixed-source terminology; see licenses/Terminology.txt and glossary-provenance.json. Project MIT does not replace upstream terms.'
    glossary['supplement_source'] = 'User-supplied acronym mappings and project overrides; unmatched historical entries are listed separately pending provenance review.'
    glossary_path.write_text(json.dumps(glossary,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf8')
    print(f'{len(lineage)-len(unknown)} exact evidence matches; {len(unknown)} historical entries require review')

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--source',type=Path,required=True)
    freeze(parser.parse_args().source)
