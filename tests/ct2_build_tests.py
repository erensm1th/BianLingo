"""Pinned archives must describe actual build sources, including cached trees."""
import hashlib
from pathlib import Path
import sys
import tempfile
import unittest
import os
from unittest.mock import patch
from zipfile import ZipFile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from build_ct2_native import verify_source_tree,unpack
from export_bergamot_source import source_data

class SourceArchiveLinks(unittest.TestCase):
    @unittest.skipUnless(os.name=='nt','Win32 normalizes symbolic-link separators')
    def test_win32_link_separator_normalization(self):
        with tempfile.TemporaryDirectory(prefix='source-link-') as t:
            root=Path(t);(root/'pkg').mkdir();(root/'LICENSE').write_bytes(b'full terms')
            link=root/'pkg/copyright';link.write_bytes(b'fixture')
            raw=b'../LICENSE';blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
            with patch.object(Path,'is_symlink',lambda p:p==link),patch('export_bergamot_source.os.readlink',return_value='..\\LICENSE'):
                self.assertEqual(source_data(root,link,blob),(b'full terms','../LICENSE'))
    def test_flattened_git_link_retains_actual_license(self):
        with tempfile.TemporaryDirectory(prefix='source-link-') as t:
            root=Path(t);(root/'pkg').mkdir();(root/'LICENSE').write_bytes(b'full upstream terms')
            raw=b'../LICENSE';link=root/'pkg/copyright';link.write_bytes(raw)
            blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
            self.assertEqual(source_data(root,link,blob),(b'full upstream terms','../LICENSE'))
            link.write_bytes(b'changed')
            with self.assertRaisesRegex(ValueError,'was modified'):source_data(root,link,blob)
    def test_tracked_link_cannot_read_outside_source(self):
        with tempfile.TemporaryDirectory(prefix='source-link-') as t:
            parent=Path(t);root=parent/'source';root.mkdir();(parent/'private').write_bytes(b'not source')
            raw=b'../private';link=root/'escape';link.write_bytes(raw)
            blob=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
            with self.assertRaisesRegex(ValueError,'escapes its root'):source_data(root,link,blob)

class SourceInputs(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='ct2-source-proof-');self.root=Path(self.tmp.name)
        self.source=self.root/'source';self.cache=self.root/'cache';self.cache.mkdir()
        archive=self.cache/'ctranslate2-source.zip'
        self.original='Duration spin_duration_ = DurationFromMilliseconds(2);\n'
        with ZipFile(archive,'w') as z:
            z.writestr('project/src/worker.cc','fixed source\n')
            z.writestr('project/third_party/ruy/ruy/thread_pool.h',self.original)
        self.lock={'sources':[dict(name='ctranslate2',destination='',format='zip',url='must-not-fetch',
            size=archive.stat().st_size,sha256=hashlib.sha256(archive.read_bytes()).hexdigest())]}
        unpack(archive,self.source/'ctranslate2','zip')
    def tearDown(self):self.tmp.cleanup()
    def test_cached_source_is_rechecked(self):
        verify_source_tree(self.source,self.lock,self.cache)
        (self.source/'ctranslate2/src/worker.cc').write_text('unrecorded modification',encoding='utf8')
        with self.assertRaisesRegex(ValueError,'modified outside recorded patches'):verify_source_tree(self.source,self.lock,self.cache)
    def test_injected_source_is_rejected(self):
        (self.source/'ctranslate2/src/injected.cc').write_text('extra',encoding='utf8')
        with self.assertRaisesRegex(ValueError,'Unrecorded file'):verify_source_tree(self.source,self.lock,self.cache)
    def test_exact_declared_patch_is_accepted(self):
        pool=self.source/'ctranslate2/third_party/ruy/ruy/thread_pool.h'
        pool.write_text(self.original.replace('Milliseconds(2);','Milliseconds(0); // BianLingo: passive chat worker'),encoding='utf8')
        verify_source_tree(self.source,self.lock,self.cache)
        pool.write_text('different patch',encoding='utf8')
        with self.assertRaisesRegex(ValueError,'Recorded source patch differs'):verify_source_tree(self.source,self.lock,self.cache)
    def test_archive_cannot_escape_source(self):
        archive=self.root/'unsafe.zip'
        with ZipFile(archive,'w') as z:z.writestr('project/../../escape.cc','bad')
        with self.assertRaisesRegex(ValueError,'Unsafe source path'):unpack(archive,self.root/'unsafe','zip')
        self.assertFalse((self.root/'escape.cc').exists())

if __name__=='__main__':unittest.main()
