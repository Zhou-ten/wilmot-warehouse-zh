"""Create a drop-in distribution from an explicit allowlist of Mod files."""
import argparse
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZipFile

ROOT = Path(__file__).resolve().parents[1]


def package(dll: Path, output: Path):
    module = dll.read_bytes()
    name = dll.name.lower()
    if (name not in {'version.dll', 'wilmzh.dll'} or not module.startswith(b'MZ') or
            b'Wilmot Chinese mod loaded. F8 toggles language in game.' not in module):
        raise ValueError('Pass the built Mod version.dll or wilmzh.dll')
    help_recipe = ROOT / 'assets/wilmzh-help.wzh'
    if not help_recipe.read_bytes().startswith(b'WHI1'):
        raise ValueError('Invalid Mod help recipe')
    entries = {
        f'WilmotPC/{name}': dll,
        'WilmotPC/wilmzh-help.wzh': help_recipe,
        'README.md': ROOT / 'README.md',
        'LICENSES/MIT.txt': ROOT / 'LICENSE',
        'LICENSES/THIRD_PARTY_NOTICES.md': ROOT / 'THIRD_PARTY_NOTICES.md',
        'LICENSES/MinHook.txt': ROOT / 'third_party/minhook/LICENSE.txt',
        'LICENSES/LodePNG.txt': ROOT / 'third_party/lodepng/LICENSE.txt',
        'LICENSES/NotoSansSC-OFL.txt': ROOT / 'third_party/noto-sans-sc/OFL.txt',
    }
    if name == 'version.dll':
        font_recipe = ROOT / 'assets/wilmzh-font.wzh'
        if not font_recipe.read_bytes().startswith(b'WZH1'):
            raise ValueError('Invalid Mod font recipe')
        entries['WilmotPC/wilmzh-font.wzh'] = font_recipe
    output.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(output, 'w', ZIP_DEFLATED, compresslevel=9) as archive:
        for name, path in entries.items():
            archive.write(path, name)
    with ZipFile(output) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != set(entries):
            raise ValueError('Archive verification failed')
    print(f'Mod-only ZIP: {output} ({output.stat().st_size:,} bytes)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dll', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    package(args.dll, args.output)
