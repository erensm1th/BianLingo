"""Exercise the real native process, including malformed framed requests."""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import shutil
import sys
import json
import hashlib
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ct2_runtime import validate_runtime, FILES

def frame(pair,text):
    name=pair.encode('ascii');body=text if isinstance(text,bytes) else text.encode('utf8')
    return struct.pack('<BBI',2,len(name),len(body))+name+body

def run(helper,catalog,models,wire):
    done=subprocess.run([str(helper),'--catalog',str(catalog),'--models-root',str(models),
                         '--bundled-root',str(models)],input=wire,capture_output=True,timeout=45)
    assert done.stdout[:6]==b'BLCT3\n',done.stderr
    result=[];p=6
    while p<len(done.stdout):
        status,size=struct.unpack_from('<BI',done.stdout,p);p+=5
        body=done.stdout[p:p+size];assert len(body)==size and size<=120000;p+=size
        result.append((status,body.decode('utf8')))
    return done.returncode,result

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--helper',type=Path,required=True)
    parser.add_argument('--models',type=Path);args=parser.parse_args();helper=args.helper.resolve()
    validate_runtime(helper.parent)
    done=subprocess.run([str(helper),'--self-test'],capture_output=True,timeout=15)
    assert done.returncode==0 and done.stdout.strip()==b'BLCT2_NATIVE_READY'
    with tempfile.TemporaryDirectory(prefix='ct2-native-') as t:
        bad_runtime=Path(t)/'damaged';bad_runtime.mkdir()
        for name in FILES:shutil.copy2(helper.parent/name,bad_runtime/name)
        (bad_runtime/'BianLingoCT2.exe').write_bytes(b'invalid PE')
        try:validate_runtime(bad_runtime)
        except ValueError:pass
        else:raise AssertionError('Corrupt native runtime accepted')
        data=(helper.parent/'BianLingoCT2.exe').read_bytes().replace(b'KERNEL32.dll',b'EVIL0000.dll')
        assert data!=(helper.parent/'BianLingoCT2.exe').read_bytes()
        (bad_runtime/'BianLingoCT2.exe').write_bytes(data);digest=hashlib.sha256(data).hexdigest()
        manifest=json.loads((bad_runtime/'native-build.json').read_text(encoding='utf8'))
        manifest['binary_sha256']=digest
        manifest['imports']=[x.replace('KERNEL32.dll','EVIL0000.dll') for x in manifest['imports']]
        (bad_runtime/'native-build.json').write_text(json.dumps(manifest),encoding='utf8')
        (bad_runtime/'engine.sha256').write_text(digest,encoding='ascii')
        try:validate_runtime(bad_runtime)
        except ValueError as error:assert 'DLL dependencies' in str(error)
        else:raise AssertionError('Self-reported DLL metadata bypassed real PE checks')
        root=Path(t)/'语言包';root.mkdir();catalog=root/'目录.json'
        catalog.write_text('{"models":[{"source":"en","target":"fr","pair":"en-fr"}]}',encoding='utf8')
        code,rows=run(helper,catalog,root,frame('../outside','hello')+frame('en-fr','hello'))
        assert code==0 and len(rows)==2 and all(status==1 for status,_ in rows)
        assert 'Unsupported' in rows[0][1] and 'not prepared' in rows[1][1]
        for wire in [b'\x02',b'\x02\xff'+struct.pack('<I',1),b'\x02\x05'+struct.pack('<I',12001),b'\xff',frame('en-fr','hello')[:-1]]:
            code,rows=run(helper,catalog,root,wire);assert code!=0 and not rows
        catalog.write_text('{"models":[{"source":"../outside","target":"fr","pair":"en-fr"}]}',encoding='utf8')
        bad=subprocess.run([str(helper),'--catalog',str(catalog),'--models-root',str(root)],input=b'',capture_output=True,timeout=10)
        assert bad.returncode and not bad.stdout
        if args.models:
            catalog=Path(__file__).resolve().parents[1]/'resources/ctranslate2-models.json'
            inputs=[('en-zh-Hans','Hello, friend.'),('zh-Hans-en','你好，朋友。'),('en-fr','Hello, friend.'),
                    ('en-zh-Hans','  Meet at [[BLG_LINK_1]].\t'),('en-fr','\u3000[[BLG_LINK_22]]\u00a0'),
                    ('en-fr',b'\xff'),('en-fr','Hello again.')]
            code,rows=run(helper,catalog,args.models.resolve(),b''.join(frame(*x) for x in inputs))
            assert code==0 and len(rows)==len(inputs)
            assert rows[0][0]==0 and rows[0][1]!='Hello, friend.'
            assert rows[1][0]==0 and 'friend' in rows[1][1].lower()
            assert rows[2][0]==0 and 'bonjour' in rows[2][1].lower()
            assert rows[3][0]==0 and rows[3][1].startswith('  ') and rows[3][1].endswith('\t') and '[[BLG_LINK_1]]' in rows[3][1]
            assert rows[4]==(0,inputs[4][1])
            assert rows[5][0]==1 and rows[6][0]==0
            unicode_models=root/'真实模型';destination=unicode_models/'models/en-fr';destination.mkdir(parents=True)
            for file in ('model.bin','config.json','shared_vocabulary.json','source.spm','target.spm'):
                shutil.copyfile(args.models/'models/en-fr'/file,destination/file)
            code,rows=run(helper,catalog,unicode_models,frame('en-fr','Hello, friend.'))
            assert code==0 and rows[0][0]==0 and 'bonjour' in rows[0][1].lower(),rows
    print('PASS real native worker: Unicode paths, bounds, invalid input, links and translation where selected')

if __name__=='__main__': main()
