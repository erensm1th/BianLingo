"""Refuse a formal artifact whose tag does not match the compiled version."""
import os
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def validate_ref(cmake, ref_type, ref_name):
    match = re.search(r'project\(BianLingo\s+VERSION\s+(\d+\.\d+\.\d+)\s', cmake)
    if not match or ref_type != 'tag' or ref_name != 'v' + match[1]:
        raise ValueError('Formal builds require the exact vX.Y.Z tag matching CMakeLists.txt')
    return match[1]

if __name__ == '__main__':
    print(validate_ref((ROOT/'CMakeLists.txt').read_text(encoding='utf8'),
                       os.environ.get('GITHUB_REF_TYPE'), os.environ.get('GITHUB_REF_NAME')))
