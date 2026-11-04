"""Render A01–A07 Chinese overlay rectangles. Original artwork stays local."""

import argparse
import io
import re
import struct
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from build_assets import fnv
from help_layout import ICONS, REGIONS

ROOT = Path(__file__).resolve().parents[1]
PAPER = (255, 252, 234, 255)
BLACK = (0, 0, 0, 255)


def make_patch(region, icons, font_file):
    x, y, right, bottom, size, text, *style = region
    style = style[0] if style else ''
    width, height = right - x, bottom - y
    fg, bg = (PAPER, BLACK)
    if style.startswith('paper') or style == 'button':
        fg, bg = ((29, 29, 27, 255) if style == 'button' else BLACK), PAPER
    image = Image.new('RGBA', (width, height), bg)
    draw = ImageDraw.Draw(image)
    copies = []
    if text:
        font = ImageFont.truetype(str(font_file), size)
        font.set_variation_by_axes([650])
        lines = []
        for line in text.split('\n'):
            runs = []
            for part in filter(None, re.split(r'(\{[^}]+\})', line)):
                if part.startswith('{'):
                    left, top, stop_x, stop_y = icons[part[1:-1]]
                    runs.append((part, stop_x - left, stop_y - top, left, top, 0, 0))
                else:
                    left, top, stop_x, stop_y = font.getbbox(part)
                    runs.append((part, max(round(font.getlength(part)), stop_x - left),
                                 stop_y - top, 0, 0, left, top))
            lines.append(runs)
        line_heights = [max([r[2] for r in row] + [size]) for row in lines]
        gap = max(2, round(size * 0.15))
        total_height = sum(line_heights) + gap * (len(lines) - 1)
        if total_height > height:
            raise ValueError(f'Text too tall for {region}: {total_height} > {height}')
        pos_y = (height - total_height)//2 if style in ('paper-center','center','button') or size >= 39 else min(3, height-total_height)
        for row, line_height in zip(lines, line_heights):
            line_width = sum(r[1] for r in row)
            if line_width > width - 2:
                raise ValueError(f'Text too wide for {region}: {line_width} > {width}')
            pos_x = (width - line_width)//2 if style.endswith('center') else 2
            for part, run_width, run_height, icon_x, icon_y, left, top in row:
                baseline = pos_y + (line_height - run_height)//2
                if part.startswith('{'):
                    copies.append((icon_x, icon_y, run_width, run_height, x+pos_x, y+baseline))
                else:
                    draw.text((pos_x-left, baseline-top), part, font=font, fill=fg)
                pos_x += run_width
            pos_y += line_height + gap
    png = io.BytesIO()
    image.save(png, format='PNG', optimize=True)
    data = png.getvalue()
    header = struct.pack('<HHHHII', x, y, width, height, len(data), len(copies))
    return header + data + b''.join(struct.pack('<6H', *entry) for entry in copies), image, copies


def build(game, font, output, preview):
    data = bytearray(struct.pack('<4sI', b'WHI1', len(REGIONS)))
    if preview:
        preview.mkdir(parents=True, exist_ok=True)
    for name, regions in REGIONS.items():
        src = game / 'lib/library' / (name + '.png')
        with Image.open(src) as im:
            original = im.convert('RGBA')
        w, h = original.size
        data.extend(struct.pack('<IHHQI', int(name, 16), w, h, fnv(src.read_bytes()), len(regions)))
        composite = original.copy() if preview else None
        for region in regions:
            x, y, right, bottom, *_ = region
            if not 0 <= x < right <= w or not 0 <= y < bottom <= h:
                raise ValueError(f'Region outside {name}: {region}')
            encoded, patch, copies = make_patch(region, ICONS.get(name, {}), font)
            data.extend(encoded)
            if composite is not None:
                composite.paste(patch, (x, y))
                for sx, sy, cw, ch, tx, ty in copies:
                    composite.paste(original.crop((sx, sy, sx+cw, sy+ch)), (tx, ty))
        if composite is not None:
            composite.save(preview / (name + '.png'))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(data)
    print(f'{len(REGIONS)} help images, {sum(map(len, REGIONS.values()))} overlay rectangles, {len(data):,} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original', type=Path, required=True, help='Vanilla WilmotPC folder')
    parser.add_argument('--noto-font', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'assets/wilmzh-help.wzh')
    parser.add_argument('--preview', type=Path, help='Local QA composites, never package these')
    args = parser.parse_args()
    build(args.original, args.noto_font, args.output, args.preview)
