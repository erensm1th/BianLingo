"""Build the standalone addon archive without desktop runtimes or user data."""
import argparse
from pathlib import Path
import re
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parents[1]
FLAVORS = {
    'Forever': '16001', 'Camelot': '16001', 'Mainline': '120100, 120105',
    'Vanilla': '11509', 'TBC': '20506', 'Mists': '50504',
    'Wrath': '30403', 'Cata': '40402',
}


def package(output):
    addon = ROOT / 'addon/BianLingo'
    base = (addon / 'BianLingo.toc').read_text(encoding='utf8').splitlines()
    version = re.search(r'^## Version: (.+)$', '\n'.join(base), re.M).group(1)
    project = (ROOT / 'CMakeLists.txt').read_text(encoding='utf8')
    if not re.search(r'project\(BianLingo VERSION ' + re.escape(version) + r'\b', project):
        raise ValueError('Addon and desktop versions differ')
    files = ['BianLingo.toc', 'LocaleMessages.lua', 'TraditionalMarkers.lua',
             'Blocklist.lua', 'PixelBridge.lua', 'Textures/Logo.tga']
    for flavor, interface in FLAVORS.items():
        name = f'BianLingo_{flavor}.toc'
        lines = (addon / name).read_text(encoding='utf8').splitlines()
        if lines != ['## Interface: ' + interface] + base[1:]:
            raise ValueError('Inconsistent client TOC: ' + name)
        files.append(name)
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix('.zip.tmp')
    try:
        with ZipFile(temporary, 'w', ZIP_DEFLATED) as archive:
            for name in files:
                archive.write(addon / name, 'BianLingo/' + name)
            archive.write(ROOT / 'LICENSE', 'BianLingo/LICENSE')
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    print(f'PASS standalone addon {version}: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    package(parser.parse_args().output)
