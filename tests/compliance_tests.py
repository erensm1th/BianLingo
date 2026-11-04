"""Release controls must reject missing evidence, not just display disclaimer text."""
import importlib.util
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
import hashlib
from unittest.mock import patch
from zipfile import ZipFile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from compliance import validate_sources, formal_gate, validate_model_evidence, terminology_policy
import package_windows
from check_release_ref import validate_ref
from export_public_snapshot import allowed, build_snapshot, inspect_snapshot, source_digest

class ReleaseRefTests(unittest.TestCase):
    def test_exact_version_tag_required(self):
        cmake = 'project(BianLingo VERSION 0.2.0 LANGUAGES CXX)'
        self.assertEqual(validate_ref(cmake, 'tag', 'v0.2.0'), '0.2.0')
        for kind,name in [('branch','v0.2.0'), ('tag','v9.9.9'), ('tag','0.2.0'), ('tag','v0.2.0-preview')]:
            with self.subTest(kind=kind,name=name), self.assertRaises(ValueError):
                validate_ref(cmake, kind, name)

class PublicSnapshotTests(unittest.TestCase):
    def test_private_paths_and_user_data_are_excluded(self):
        for name in ['AGENTS.md','src/AGENTS.md','.git/config','docs/INTERNAL_NOTES.md',
                     'UserData/settings.json','.local/research.txt','dist/output.exe']:
            self.assertFalse(allowed(name), name)
        for name in ['resources/data/licenses/Terminology.txt','src/engine.cpp','addon/BianLingo/Blocklist.lua']:
            self.assertTrue(allowed(name), name)
        for name in ['src/../secret.txt','src/token.ed25519','src/UserData/settings.json']:
            with self.assertRaises(ValueError): allowed(name)

    def test_export_keeps_notices_and_review_status_but_not_private_files(self):
        with tempfile.TemporaryDirectory(prefix='public-snapshot-') as temporary:
            root=Path(temporary); names=['LICENSE','CMakeLists.txt','README.md','resources/release-review.json',
                                        'docs/INTERNAL_NOTES.md','UserData/settings.json']
            for name in names:
                p=root/name;p.parent.mkdir(parents=True,exist_ok=True)
                p.write_text('{"releaseStatus":"blocked","pending":[{"id":"fixture"}]}' if name.endswith('release-review.json') else
                             'notice\n' if name=='README.md' else 'fixture',encoding='utf8')
            output=root/'source.zip'; result=build_snapshot(root,names,output,'synthetic-revision')
            with ZipFile(output) as archive:
                self.assertNotIn('docs/INTERNAL_NOTES.md',archive.namelist())
                self.assertNotIn('UserData/settings.json',archive.namelist())
                self.assertIn('LICENSE',archive.namelist());self.assertIn('SOURCE_SNAPSHOT.json',archive.namelist())
            self.assertEqual(result['releaseReview']['releaseStatus'],'blocked')
            self.assertEqual(inspect_snapshot(output)['files'],len(result['files']))
            with self.assertRaises(FileExistsError): build_snapshot(root,names,output,'synthetic-revision')

    def test_review_fingerprint_changes_only_with_eligible_source(self):
        with tempfile.TemporaryDirectory(prefix='source-fingerprint-') as t:
            root=Path(t);(root/'resources').mkdir();names=['README.md','resources/release-review.json']
            (root/'README.md').write_bytes(b'reviewed\r\n')
            (root/'resources/release-review.json').write_text('{}',encoding='utf8')
            original=source_digest(root,names)
            (root/'resources/release-review.json').write_text('{"status":"approved"}',encoding='utf8')
            (root/'README.md').write_bytes(b'reviewed\n')
            self.assertEqual(source_digest(root,names),original)
            (root/'README.md').write_text('changed',encoding='utf8')
            self.assertNotEqual(source_digest(root,names),original)

class ComplianceTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='bianlingo-compliance-')
        self.root=Path(self.tmp.name)
        shutil.copytree(ROOT/'resources',self.root/'resources')
        shutil.copytree(ROOT/'addon',self.root/'addon')
    def tearDown(self): self.tmp.cleanup()
    def edit(self,name,fn):
        path=self.root/'resources'/name
        data=json.loads(path.read_text(encoding='utf8'));fn(data)
        path.write_text(json.dumps(data),encoding='utf8')
    def test_original_conflicts_recorded(self):
        data=json.loads((self.root/'resources/ctranslate2-models.json').read_text())
        conflicts={m['pair'] for m in data['models'] if m['license']=='CC-BY-4.0'}
        self.assertEqual(conflicts,{'en-de','ru-en','zh-en'})
        validate_sources(self.root)
    def test_unknown_license_rejected(self):
        self.edit('ctranslate2-models.json',lambda x:x['models'][0].update(license='unverified'))
        with self.assertRaises(ValueError): validate_sources(self.root)
    def test_notice_tampering_rejected(self):
        (self.root/'resources/licenses/CTranslate2.txt').write_text('MIT',encoding='utf8')
        with self.assertRaises(ValueError): validate_sources(self.root)
    def test_changed_translation_cannot_keep_old_attribution(self):
        lock=json.loads((self.root/'resources/data/glossary-provenance.json').read_text(encoding='utf8'))
        key=next(k for k,origins in lock['entries'].items() if origins)
        self.edit('data/glossary_zhCN.json',lambda x:x['terms'].update({key:'changed translation'}))
        with self.assertRaisesRegex(ValueError,'claimed source'): validate_sources(self.root)
    def test_glossary_snapshot_tampering_rejected(self):
        (self.root/'resources/data/provenance-source/Locales/zhCN.lua').write_text('-- changed',encoding='utf8')
        with self.assertRaises(ValueError): validate_sources(self.root)
    def test_formal_release_is_blocked(self):
        self.edit('release-review.json',lambda x:x.update(releaseStatus='blocked',pending=[{'id':'fixture-review'}]))
        with self.assertRaisesRegex(ValueError,'fixture-review'): formal_gate(self.root)
    def test_clearing_review_list_does_not_bypass_model_evidence(self):
        self.edit('release-review.json',lambda x:x.update(releaseStatus='approved',pending=[]))
        self.edit('model-evidence/ctranslate2/index.json',lambda x:x['models'][0].update(review_status='collected-not-approved'))
        with self.assertRaisesRegex(ValueError,'Reviewed model evidence'): formal_gate(self.root)

    def test_pinned_card_tampering_rejected(self):
        p=self.root/'resources/model-evidence/ctranslate2/en-de/upstream-README.md'
        p.write_text('unverified replacement',encoding='utf8')
        with self.assertRaisesRegex(ValueError,'Pinned model evidence'):validate_model_evidence(self.root)
    def test_no_invented_original_revision(self):
        self.edit('ctranslate2-models.json',lambda x:x['models'][0].update(original_weight_revision='0'*40))
        with self.assertRaisesRegex(ValueError,'original checkpoint'):validate_model_evidence(self.root)
    def test_bounded_terminology_deferral(self):
        terminology_policy(self.root)
        self.edit('data/glossary-provenance.json',lambda x:x['unresolved_entries'].update(extra='fixture'))
        with self.assertRaisesRegex(ValueError,'exact, documented'):terminology_policy(self.root)

    def test_native_source_notice_change_rejected(self):
        (self.root/'resources/licenses/CT2Native.txt').write_text('incomplete',encoding='utf8')
        with self.assertRaisesRegex(ValueError,'Native runtime source/notice'):validate_sources(self.root)
    def test_optional_runtime_inventory_change_rejected(self):
        self.edit('model-evidence/ollama/archive-audit.json',lambda x:x['members'].pop())
        with self.assertRaisesRegex(ValueError,'publisher-direct runtime evidence'):validate_sources(self.root)

class PackageTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='bianlingo-package-evidence-')
        self.root=Path(self.tmp.name);self.stage=self.root/'stage';self.output=self.root/'package.zip'
        self.native_patch=patch.object(package_windows,"validate_runtime");self.native_patch.start()
        def embed(executable, payload, metadata):
            self.payload = payload
            self.metadata = json.loads(metadata)
        self.bundle_patch=patch.object(package_windows,"embed_resources",side_effect=embed);self.bundle_patch.start()
        self.original_root=package_windows.ROOT
        self.resource=self.root/'resources';self.resource.mkdir()
        def put(name,data):
            path=self.stage/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
        self.put=put
        for name in ('LICENSE','THIRD_PARTY_NOTICES.md','PROJECT_NOTICE.md','PRIVACY.md','SOURCE_ACCESS.md'):
            (self.root/name).write_bytes((ROOT/name).read_bytes());put(name,(ROOT/name).read_bytes())
        for name in ('BianLingo.toc','PixelBridge.lua','LocaleMessages.lua','TraditionalMarkers.lua','Blocklist.lua'):
            put('BianLingo/'+name,b'fixture')
        logo=(ROOT/'addon/BianLingo/Textures/Logo.tga').read_bytes()
        source_logo=self.root/'addon/BianLingo/Textures/Logo.tga'
        source_logo.parent.mkdir(parents=True);source_logo.write_bytes(logo)
        (source_logo.parent.parent/'BianLingo.toc').write_bytes(b'fixture')
        put('BianLingo/Textures/Logo.tga',logo)
        put('BianLingo.exe',b'synthetic application fixture')
        for name in package_windows.CT2_FILES:put('data/ctranslate2/'+name,b'native fixture')
        put('native-build.json',b'{}')
        review=(ROOT/'resources/release-review.json').read_bytes()
        (self.resource/'release-review.json').write_bytes(review);put('RELEASE_REVIEW.json',review)
        model=json.dumps({'models':[{'pair':'en-zh-Hans','name':'test','compressed_sha256':hashlib.sha256(b'compressed fixture').hexdigest()}]}).encode()
        (self.resource/'bergamot-models.json').write_bytes(model);put('data/bergamot/models.json',model)
        put('data/bergamot/compressed/en-zh-Hans/test.zst',b'compressed fixture')
        helper=b'synthetic helper fixture';helper_hash=hashlib.sha256(helper).hexdigest()
        put('data/bergamot/BianLingoBergamot.exe',helper)
        put('data/bergamot/engine.sha256',helper_hash.encode())
        put('data/bergamot/engine-LICENSES.txt',b'fixture notices')
        names=['bergamot-source/LICENSE','project/tools/bergamot_helper/src/main.cpp','project/tools/build_bergamot.py',
            'project/tools/bergamot_helper/CMakeLists.txt','bergamot-source/3rd_party/marian-dev/src/common/git_revision.h',
            'bergamot-source/3rd_party/ssplit-cpp/src/3rd-party/pcre2-10.39/LICENCE']
        archive=self.stage/'data/bergamot/corresponding-source.zip'
        with ZipFile(archive,'w') as z:
            for name in names:z.writestr(name,b'synthetic source')
            z.writestr('README.txt','Synthetic source fixture only')
            z.writestr('SOURCE_FILES.json',json.dumps({'binary_sha256':helper_hash,'files':[
                {'path':name,'size':len(b'synthetic source'),'sha256':hashlib.sha256(b'synthetic source').hexdigest()} for name in names]}))
        put('data/bergamot/source-manifest.json',json.dumps({'filename':archive.name,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'binary_sha256':helper_hash}).encode())
    def tearDown(self): self.bundle_patch.stop();self.native_patch.stop();self.tmp.cleanup()
    def package(self,*options):
        with patch.object(package_windows,'ROOT',self.root),patch.object(sys,'argv',[
            'package_windows.py','--root',str(self.stage),'--output',str(self.output),*options]):
            package_windows.main()
    def test_valid_evidence_layout(self):
        self.package()
        with ZipFile(self.output) as z:
            self.assertEqual({n.split('/')[0] for n in z.namelist()}, {'BianLingo.exe','AddOns','data'})
            self.assertIn('AddOns/BianLingo/LICENSE',z.namelist())
            self.assertIn('data/Notices/artifact-inventory.json',z.namelist())
            self.assertIn('data/bergamot/corresponding-source.zip',z.namelist())
            self.assertNotIn('native-build.json',z.namelist())
    def test_personal_data_never_enters_inventory_or_package(self):
        self.put('UserData/settings.json',b'private fixture')
        with self.assertRaisesRegex(ValueError,'personal data'):self.package()
        self.assertFalse(self.output.exists());self.assertFalse((self.stage/'artifact-inventory.json').exists())
    def test_fallback_lua_sync_file_is_rejected(self):
        self.put('BianLingo/BlocklistSync.lua',b'fallback personal blocklist')
        with self.assertRaisesRegex(ValueError,'personal data'): self.package()
        self.assertFalse(self.output.exists())
    def test_changed_model_rejected(self):
        self.put('data/bergamot/compressed/en-zh-Hans/test.zst',b'corrupted')
        with self.assertRaisesRegex(ValueError,'model hash mismatch'):self.package()
    def test_mismatched_helper_rejected(self):
        self.put('data/bergamot/BianLingoBergamot.exe',b'different helper')
        with self.assertRaisesRegex(ValueError,'different helper'):self.package()
    def test_stale_review_notice_rejected(self):
        self.put('RELEASE_REVIEW.json',b'{"releaseStatus":"approved"}')
        with self.assertRaisesRegex(ValueError,'Stale package'):self.package()
    def test_formal_option_blocks_packaging(self):
        with patch.object(package_windows,'formal_gate',side_effect=ValueError('Formal release blocked: fixture')):
            with self.assertRaisesRegex(ValueError,'Formal release blocked'):self.package('--formal')
        self.assertFalse(self.output.exists())

if __name__=='__main__': unittest.main()
