"""将 PNG 壁纸转换为 LVGL RGB565 C 数组(320x240, cover 裁剪居中)。

用法:
    python tools/convert_wallpaper.py --input 壁纸.png --output OTA/MDK-ARM/UI/ui_wallpaper.c --name ui_wallpaper
生成的 C 文件可直接加入 Keil 工程,用 lv_image_create + lv_image_set_src 显示。
"""

import argparse
from pathlib import Path

from PIL import Image

W = 320
H = 240


def cover_resize(img, w, h):
    """保持宽高比缩放并居中裁剪,铺满目标尺寸(cover 模式)。"""
    scale = max(w / img.width, h / img.height)
    nw = round(img.width * scale)
    nh = round(img.height * scale)
    img = img.resize((nw, nh), Image.Resampling.LANCZOS)
    left = (nw - w) // 2
    top = (nh - h) // 2
    return img.crop((left, top, left + w, top + h))


def to_rgb565(px):
    r, g, b = px[0], px[1], px[2]
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, help="输入 PNG 路径")
    parser.add_argument("--output", required=True, help="输出 C 文件路径")
    parser.add_argument("--name", default="ui_wallpaper", help="C 符号名")
    args = parser.parse_args()

    img = Image.open(args.input).convert("RGB")
    img = cover_resize(img, W, H)
    pixels = img.load()

    data = bytearray()
    for y in range(H):
        for x in range(W):
            value = to_rgb565(pixels[x, y])
            data.append(value & 0xFF)      # 小端:低字节在前
            data.append(value >> 8)

    lines = []
    for index in range(0, len(data), 16):
        chunk = data[index:index + 16]
        lines.append("    " + ", ".join(f"0x{value:02x}" for value in chunk) + ",")

    output = f"""/* 由 tools/convert_wallpaper.py 自动生成,请勿手工修改。 */
/* 尺寸 {W}x{H},RGB565 小端,LCD 直接可显示。 */
#include "lvgl.h"

static const uint8_t {args.name}_data[] = {{
{chr(10).join(lines)}
}};

const lv_image_dsc_t {args.name} = {{
    .header.magic = LV_IMAGE_HEADER_MAGIC,
    .header.cf = LV_COLOR_FORMAT_RGB565,
    .header.w = {W},
    .header.h = {H},
    .header.stride = {W} * 2,
    .data_size = sizeof({args.name}_data),
    .data = {args.name}_data,
}};
"""
    Path(args.output).write_text(output, encoding="utf-8", newline="\n")

    header = f"""#ifndef __{args.name.upper()}_H__
#define __{args.name.upper()}_H__

#include "lvgl.h"

/* {W}x{H} RGB565 壁纸,由 tools/convert_wallpaper.py 生成 */
extern const lv_image_dsc_t {args.name};

#endif /* __{args.name.upper()}_H__ */
"""
    header_path = Path(args.output).with_suffix(".h")
    header_path.write_text(header, encoding="utf-8", newline="\n")
    print(f"壁纸转换完成: {W}x{H} RGB565, {len(data)} 字节 -> {args.output} (+ {header_path.name})")


if __name__ == "__main__":
    main()
