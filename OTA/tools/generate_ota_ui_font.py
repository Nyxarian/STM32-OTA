"""根据 OTA 界面源码中的字符串生成精简 LVGL 中文字体。"""

import argparse
import math
import re
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


# 匹配 C 源码中所有字符串字面量(包括 UI_TEXT_* 宏与直接写出的字符串)。
UI_TEXT_LITERAL = re.compile(
    r'"((?:\\.|[^"\\])*)"',
    re.MULTILINE,
)


def decode_c_literal(literal):
    data = bytearray()
    text = literal[1:-1]
    index = 0
    escapes = {"n": 10, "r": 13, "t": 9, "\\": 92, '"': 34, "0": 0}
    while index < len(text):
        char = text[index]
        if char != "\\":
            data.extend(char.encode("utf-8"))
            index += 1
            continue
        index += 1
        if index >= len(text):
            break
        escape = text[index]
        if escape == "x" and index + 2 < len(text):
            data.append(int(text[index + 1:index + 3], 16))
            index += 3
        else:
            data.append(escapes.get(escape, ord(escape)))
            index += 1
    return data.decode("utf-8", errors="ignore")


def collect_symbols(source_paths):
    symbols = set()
    for source_path in source_paths:
        source = Path(source_path).read_text(encoding="utf-8")
        for match in UI_TEXT_LITERAL.finditer(source):
            for char in decode_c_literal(match.group(0)):
                if ord(char) >= 32:
                    symbols.add(char)
    # 强制包含全部 ASCII 可打印字符,保证新增英文/数字不缺字。
    symbols.update(chr(code) for code in range(0x20, 0x7F))
    if not symbols:
        raise ValueError("未找到字符串字面量")
    return symbols


def pack_2bpp(values):
    packed = bytearray()
    for index in range(0, len(values), 4):
        byte = 0
        for offset in range(4):
            value = values[index + offset] if index + offset < len(values) else 0
            level = min(3, (value + 42) // 85)
            byte |= level << (6 - offset * 2)
        packed.append(byte)
    return packed


def format_bytes(values):
    lines = []
    for index in range(0, len(values), 16):
        chunk = values[index:index + 16]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")
    return "\n".join(lines)


def format_u16(values):
    lines = []
    for index in range(0, len(values), 12):
        chunk = values[index:index + 12]
        lines.append("    " + ", ".join(f"0x{value:04x}" for value in chunk) + ",")
    return "\n".join(lines)


def italicize(image, degrees):
    if image.width == 0 or image.height == 0 or abs(degrees) < 0.01:
        return image
    slope = math.tan(math.radians(degrees))
    maximum_shift = int(math.ceil(image.height * slope))
    output = Image.new("L", (image.width + maximum_shift, image.height), 0)
    for row in range(image.height):
        shift = int(round((image.height - 1 - row) * slope))
        output.paste(image.crop((0, row, image.width, row + 1)), (shift, row))
    return output


def load_font(path, size, variation=None):
    font = ImageFont.truetype(str(path), size=size)
    if variation is not None and hasattr(font, "set_variation_by_name"):
        font.set_variation_by_name(variation.encode("ascii"))
    return font


def render_glyph(char, latin_font, cjk_font, degrees):
    use_latin = ord(char) < 0x300
    font = latin_font if use_latin else cjk_font
    scale_x = 1.0 if use_latin else 0.92
    advance = max(1, round(font.getlength(char) * scale_x * 16))
    bbox = font.getbbox(char, anchor="ls")
    if bbox is None or bbox[2] <= bbox[0] or bbox[3] <= bbox[1]:
        return bytearray(), advance, 0, 0, 0, 0

    left, top, right, bottom = bbox
    width = right - left
    height = bottom - top
    image = Image.new("L", (width, height), 0)
    draw = ImageDraw.Draw(image)
    draw.text((-left, -top), char, font=font, fill=255, anchor="ls")

    if scale_x != 1.0:
        scaled_width = max(1, round(image.width * scale_x))
        image = image.resize((scaled_width, image.height), Image.Resampling.LANCZOS)
    image = italicize(image, degrees)
    # 兼容新版和旧版 Pillow 的像素读取接口。
    pixel_data = image.get_flattened_data() if hasattr(image, "get_flattened_data") else image.getdata()
    bitmap = pack_2bpp(list(pixel_data))
    return bitmap, advance, image.width, image.height, round(left * scale_x), -bottom


def build_font_block(name, symbols, latin_path, cjk_path, size, degrees,
                     latin_grow, cjk_grow):
    selected = sorted(symbols, key=ord)
    latin_font = load_font(latin_path, size + latin_grow, "Bold Condensed")
    cjk_font = load_font(cjk_path, size + cjk_grow)
    latin_ascent, latin_descent = latin_font.getmetrics()
    cjk_ascent, cjk_descent = cjk_font.getmetrics()
    line_height = max(latin_ascent + latin_descent, cjk_ascent + cjk_descent)
    base_line = max(latin_descent, cjk_descent)

    bitmap = bytearray()
    descriptors = [
        "    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0},"
    ]
    for char in selected:
        offset = len(bitmap)
        glyph_bitmap, advance, width, height, ofs_x, ofs_y = render_glyph(
            char, latin_font, cjk_font, degrees
        )
        bitmap.extend(glyph_bitmap)
        descriptors.append(
            "    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, .ofs_x = %d, .ofs_y = %d},"
            % (offset, advance, width, height, ofs_x, ofs_y)
        )

    range_start = ord(selected[0])
    offsets = [ord(char) - range_start for char in selected]
    block = f'''static LV_ATTRIBUTE_LARGE_CONST const uint8_t glyph_bitmap_{name}[] = {{
{format_bytes(bitmap)}
}};

static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc_{name}[] = {{
{chr(10).join(descriptors)}
}};

static const uint16_t unicode_list_{name}[] = {{
{format_u16(offsets)}
}};

static const lv_font_fmt_txt_cmap_t cmaps_{name}[] = {{
    {{
        .range_start = {range_start},
        .range_length = {ord(selected[-1]) - range_start + 1},
        .glyph_id_start = 1,
        .unicode_list = unicode_list_{name},
        .glyph_id_ofs_list = NULL,
        .list_length = {len(selected)},
        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY
    }}
}};

static const lv_font_fmt_txt_dsc_t font_dsc_{name} = {{
    .glyph_bitmap = glyph_bitmap_{name},
    .glyph_dsc = glyph_dsc_{name},
    .cmaps = cmaps_{name},
    .kern_dsc = NULL,
    .kern_scale = 0,
    .cmap_num = 1,
    .bpp = 2,
    .kern_classes = 0,
    .bitmap_format = 0
}};

const lv_font_t ui_font_{name} = {{
    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,
    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,
    .line_height = {line_height},
    .base_line = {base_line},
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .static_bitmap = 0,
    .dsc = &font_dsc_{name},
    .fallback = NULL,
    .user_data = NULL
}};
'''
    return block, len(selected), len(bitmap)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", nargs="+", required=True)
    parser.add_argument("--latin-font", required=True)
    parser.add_argument("--cjk-font", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--slant", type=float, default=0.0)
    parser.add_argument("--latin-grow", type=int, default=2)
    parser.add_argument("--cjk-grow", type=int, default=1)
    parser.add_argument("--sizes", type=int, nargs="+", default=[8, 12, 14],
                        help="要生成的字体字号列表(像素)")
    args = parser.parse_args()

    symbols = collect_symbols(args.source)
    blocks = []
    stats = []
    # 按 --sizes 生成多套字号,每套只包含实际会用到的一套字形。
    for size in args.sizes:
        block, count, bitmap_size = build_font_block(
            str(size), symbols, args.latin_font, args.cjk_font,
            size, args.slant, args.latin_grow, args.cjk_grow
        )
        blocks.append(block)
        stats.append(f"{size}px: {count} glyphs, {bitmap_size} bitmap bytes")

    output = f"""/* 由 tools/generate_ota_ui_font.py 自动生成，请勿手工修改。 */
/* 字体组合：Bahnschrift Bold Condensed + 等线 Bold，2bpp。 */
#include "lvgl.h"

""" + "\n".join(blocks)
    Path(args.output).write_text(output, encoding="utf-8", newline="\n")
    print("字体生成完成：" + "; ".join(stats))


if __name__ == "__main__":
    main()
