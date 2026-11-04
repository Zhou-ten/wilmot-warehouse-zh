"""Build a Mod-only font recipe. No standalone original or merged font is emitted."""
import argparse
import hashlib
import io
import json
import struct
import xml.etree.ElementTree as ET
from pathlib import Path

from fontTools.pens.recordingPen import DecomposingRecordingPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont
from fontTools.ttLib.sfnt import SFNTWriter
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
DIMENSION = 2048
ATLAS_Y = 270
WEIGHT = 650


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return value


def merged_ttf(original_bytes, cjk_path, chars):
    with TTFont(io.BytesIO(original_bytes), recalcTimestamp=False) as font, TTFont(cjk_path) as cjk:
        copyright_text = cjk['name'].getDebugName(0) or ''
        if 'Adobe' not in copyright_text or cjk['name'].getDebugName(1) != 'Noto Sans SC':
            raise ValueError('Use the OFL Noto Sans SC variable TTF specified in the README')
        original_glyf = font.reader['glyf']
        original_offsets = list(font['loca'].locations)
        original_glyf_offset = font.reader.tables['glyf'].offset
        glyphs = cjk.getGlyphSet(location={'wght': WEIGHT})
        cmap = cjk.getBestCmap()
        scale = font['head'].unitsPerEm / cjk['head'].unitsPerEm
        order = font.getGlyphOrder()
        original_cmap = font.getBestCmap().copy()
        for ch in chars:
            code = ord(ch)
            if code in original_cmap:
                continue
            name = f'zh{code:04X}'
            source = glyphs[cmap[code]]
            recording = DecomposingRecordingPen(glyphs)
            source.draw(recording)
            pen = TTGlyphPen(None)
            recording.replay(TransformPen(pen, (scale, 0, 0, scale, 0, 0)))
            font['glyf'][name] = pen.glyph()
            font['hmtx'][name] = (round(source.width * scale), round(source.lsb * scale))
            if name not in order:
                order.append(name)
            for table in font['cmap'].tables:
                if table.isUnicode() and table.format in (4, 12):
                    table.cmap[code] = name
        font.setGlyphOrder(order)
        face = font['name'].getDebugName(4)
        names = {1: face, 2: 'Regular', 4: face, 16: face, 17: 'Regular'}
        for record in list(font['name'].names):
            if record.nameID in names:
                font['name'].setName(names[record.nameID], record.nameID, record.platformID,
                                     record.platEncID, record.langID)
        if 'DSIG' in font:
            del font['DSIG']
        stream = io.BytesIO()
        font.save(stream)

    # Reuse the exact original encoded glyph table. Only the appended CJK
    # outlines are emitted as new data by the recipe generator below.
    stream.seek(0)
    with TTFont(stream, lazy=True) as merged:
        new_offsets = list(merged['loca'].locations)
        first_new = new_offsets[len(original_offsets) - 1]
        prefix = original_glyf[:original_offsets[-1]]
        tail = merged.reader['glyf'][first_new:]
        offsets = original_offsets + [len(prefix) + x - first_new
                                      for x in new_offsets[len(original_offsets):]]
        tables = {tag: merged.reader[tag] for tag in merged.reader.keys()}
        tables['glyf'] = prefix + tail
        tables['loca'] = struct.pack(f'>{len(offsets)}I', *offsets)
        head = bytearray(tables['head'])
        struct.pack_into('>h', head, 50, 1)  # 32-bit loca offsets
        tables['head'] = bytes(head)
        result = io.BytesIO()
        writer = SFNTWriter(result, len(tables), merged.sfntVersion)
        for tag, data in sorted(tables.items()):
            writer[tag] = data
        writer.close()
        rebuilt_bytes = result.getvalue()
        result.seek(0)
        with TTFont(result, lazy=True) as rebuilt:
            reuse_at = rebuilt.reader.tables['glyf'].offset
        return rebuilt_bytes, (reuse_at, original_glyf_offset, len(prefix))


def delta(original, target, forced_copy):
    """Encode bounded copy ranges and literal bytes. Force original outlines to be references."""
    size = 16
    index = {}
    for i in range(len(original) - size + 1):
        index.setdefault(original[i:i + size], []).append(i)
    instructions = bytearray()
    new_bytes = bytearray()

    def flush():
        if new_bytes:
            instructions.extend(struct.pack('<I', 0x80000000 | len(new_bytes)))
            instructions.extend(new_bytes)
            new_bytes.clear()

    force_at, force_from, force_size = forced_copy
    assert target[force_at:force_at + force_size] == original[force_from:force_from + force_size]
    i = 0
    while i < len(target):
        if i == force_at:
            offset, length = force_from, force_size
        else:
            end = force_at if i < force_at else len(target)
            offset, length = 0, 0
            if i + size <= end:
                for start in index.get(target[i:i + size], ())[:16]:
                    n = size
                    limit = min(end - i, len(original) - start)
                    while n < limit and target[i + n] == original[start + n]:
                        n += 1
                    if n > length:
                        offset, length = start, n
        if length:
            flush()
            instructions.extend(struct.pack('<II', length, offset))
            i += length
        else:
            new_bytes.append(target[i])
            i += 1
    flush()
    return instructions


def bitmap_additions(xml_bytes, cjk_path, chars):
    existing = {int(g.attrib['id']) for g in ET.fromstring(xml_bytes).find('chars')}
    image = Image.new('RGBA', (DIMENSION, DIMENSION - ATLAS_Y))
    draw = ImageDraw.Draw(image)
    cjk = ImageFont.truetype(str(cjk_path), 52)
    cjk.set_variation_by_axes([WEIGHT])
    additions = []
    x, y = 2, 0
    for ch in chars:
        if ord(ch) in existing:
            continue
        if x + 60 >= DIMENSION:
            x, y = 2, y + 64
        if y + 64 >= image.height:
            raise ValueError('Translation characters exceed the atlas capacity')
        draw.text((x + 2, y + 51), ch, font=cjk, anchor='ls', fill='white')
        additions.append(f'    <char id="{ord(ch)}" x="{x}" y="{y + ATLAS_Y}" width="56" '
                         'height="64" xoffset="0" yoffset="0" xadvance="52" page="0" chnl="15"/>\n')
        x += 60
    png = io.BytesIO()
    image.save(png, format='PNG', optimize=True)
    return len(existing) + len(additions), ''.join(additions).encode('ascii'), png.getvalue()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', required=True, type=Path, help='Local vanilla WilmotPC folder')
    parser.add_argument('--noto-font', required=True, type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/wilmzh-font.wzh')
    args = parser.parse_args()
    hashes = json.loads((ROOT / 'tools/original_hashes.json').read_text(encoding='utf-8'))
    ttf, xml, atlas = (args.original / name for name in ('fonts/DIN.ttf', 'xml/din.xml', 'atlases/din_0.png'))
    for file in (ttf, xml, atlas):
        if hashlib.sha256(file.read_bytes()).hexdigest() != hashes[file.relative_to(args.original).as_posix()]:
            raise ValueError(f'Unsupported original file: {file.name}')
    original_ttf, original_xml, original_png = ttf.read_bytes(), xml.read_bytes(), atlas.read_bytes()
    translations = json.loads((ROOT / 'translations/zh-CN.json').read_text(encoding='utf-8'))
    chars = sorted({ch for text in translations.values() for ch in text if ord(ch) > 127})
    target, original_glyph_block = merged_ttf(original_ttf, args.noto_font, chars)
    font_delta = delta(original_ttf, target, original_glyph_block)
    count, xml_additions, glyph_png = bitmap_additions(original_xml, args.noto_font, chars)
    header = struct.pack('<4sQQQIIII', b'WZH1', fnv(original_ttf), fnv(original_xml),
                         fnv(original_png), count, len(font_delta), len(xml_additions), len(glyph_png))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + font_delta + xml_additions + glyph_png)
    print(f'Mod-only recipe built: {len(chars)} characters, {args.output.stat().st_size:,} bytes')


if __name__ == '__main__':
    main()
