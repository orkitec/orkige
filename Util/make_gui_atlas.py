#!/usr/bin/env python3
"""Generate the gui texture atlas (PNG + .ogui) - stdlib only.

The historical pipeline (orkige_fontconverter/ + bmfontgen.exe + sspack.exe,
Windows-only, 2012) is gone; this script replaces it with a deterministic,
license-clean generator:

  * a hand-designed 5x7 pixel font (ASCII 32-126) rasterized at 2x
    ([Font.9], 10x14 glyphs, HUD text) and 4x ([Font.24], 20x28 glyphs,
    titles/banners),
  * procedurally drawn UI sprites (rounded rects with borders): panel,
    button + _over/_down/_disabled states, progressbar frame and
    progressbar_bar fill (the name GuiProgressBar hardcodes),
    checkbox_off / checkbox_on (the settings toggle states) and the
    select_menu_field / _left / _right fields the SelectMenu+Slider use,
  * the designated 4x4 whitepixel block the UI renderer uses for solid fills.

.ogui format (what Orkige::UiAtlas actually parses - see
orkige_engine/engine_gui/UiAtlas.cpp; inherited from the retired
Gorilla library):

  [Texture]
  file <texture filename>            # loaded from the same resource group
  whitepixel <x> <y>                 # pixel coords of a white opaque texel
  [Font.<glyphDataIndex>]
  offset <x> <y>                     # added to every glyph_<n> x/y
  lineheight <px>  baseline <px>  spacelength <px>  monowidth <px>
  range <first> <last>               # inclusive glyph code range
  glyph_<code> <x> <y> <w> <h> [advance]   # pixel rect + horizontal advance
  kerning_<left> <right> <adjust>    # optional kerning pairs
  [Sprites]
  <name> <x> <y> <w> <h>             # pixel rect (parsed as unsigned ints!)

Semantics inherited from Gorilla (now UiAtlas/UiRenderer) + the legacy fontconverter:
  * Caption/MarkupText draw every glyph TOP-aligned at the cursor and advance
    by "advance + kerning" (kerning falls back to letterspacing, default 0),
    so every glyph cell here has the full font height (no vertical trim) and
    the advance carries the 1px letter gap.
  * ' ' never renders; the cursor advances by spacelength instead.
  * sprite/glyph coords are texture pixels; UiAtlas converts to UVs itself.

The script self-validates: it re-parses its own .ogui output with the same
rules as Ogre::ConfigFile/UiAtlas and checks every rect against the texture
bounds, an occupancy grid (no overlaps) and the whitepixel. Exits non-zero
on any violation.

Usage: make_gui_atlas.py <output_dir> [atlas_name]
       (default atlas name: gui_default)
"""

import os
import struct
import sys
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import orkige_sidecar  # noqa: E402  (sibling stdlib helper)

# ---------------------------------------------------------------------------
# 5x7 pixel font, ASCII 33..126 ('X' = set). Rows top to bottom; baseline is
# the bottom row (6) - descenders are compressed, classic LCD-font style.
# ---------------------------------------------------------------------------

FONT_5X7 = {
    '!': ["..X..", "..X..", "..X..", "..X..", "..X..", ".....", "..X.."],
    '"': [".X.X.", ".X.X.", ".X.X.", ".....", ".....", ".....", "....."],
    '#': [".X.X.", ".X.X.", "XXXXX", ".X.X.", "XXXXX", ".X.X.", ".X.X."],
    '$': ["..X..", ".XXXX", "X.X..", ".XXX.", "..X.X", "XXXX.", "..X.."],
    '%': ["XX...", "XX..X", "...X.", "..X..", ".X...", "X..XX", "...XX"],
    '&': [".XX..", "X..X.", "X.X..", ".X...", "X.X.X", "X..X.", ".XX.X"],
    "'": ["..X..", "..X..", "..X..", ".....", ".....", ".....", "....."],
    '(': ["...X.", "..X..", ".X...", ".X...", ".X...", "..X..", "...X."],
    ')': [".X...", "..X..", "...X.", "...X.", "...X.", "..X..", ".X..."],
    '*': [".....", "..X..", "X.X.X", ".XXX.", "X.X.X", "..X..", "....."],
    '+': [".....", "..X..", "..X..", "XXXXX", "..X..", "..X..", "....."],
    ',': [".....", ".....", ".....", ".....", "..XX.", "..X..", ".X..."],
    '-': [".....", ".....", ".....", "XXXXX", ".....", ".....", "....."],
    '.': [".....", ".....", ".....", ".....", ".....", "..XX.", "..XX."],
    '/': ["....X", "...X.", "..X..", ".X...", "X....", ".....", "....."],
    '0': [".XXX.", "X...X", "X..XX", "X.X.X", "XX..X", "X...X", ".XXX."],
    '1': ["..X..", ".XX..", "..X..", "..X..", "..X..", "..X..", ".XXX."],
    '2': [".XXX.", "X...X", "....X", "...X.", "..X..", ".X...", "XXXXX"],
    '3': ["XXXXX", "...X.", "..X..", "...X.", "....X", "X...X", ".XXX."],
    '4': ["...X.", "..XX.", ".X.X.", "X..X.", "XXXXX", "...X.", "...X."],
    '5': ["XXXXX", "X....", "XXXX.", "....X", "....X", "X...X", ".XXX."],
    '6': ["..XX.", ".X...", "X....", "XXXX.", "X...X", "X...X", ".XXX."],
    '7': ["XXXXX", "....X", "...X.", "..X..", ".X...", ".X...", ".X..."],
    '8': [".XXX.", "X...X", "X...X", ".XXX.", "X...X", "X...X", ".XXX."],
    '9': [".XXX.", "X...X", "X...X", ".XXXX", "....X", "...X.", ".XX.."],
    ':': [".....", "..XX.", "..XX.", ".....", "..XX.", "..XX.", "....."],
    ';': [".....", "..XX.", "..XX.", ".....", "..XX.", "..X..", ".X..."],
    '<': ["...X.", "..X..", ".X...", "X....", ".X...", "..X..", "...X."],
    '=': [".....", ".....", "XXXXX", ".....", "XXXXX", ".....", "....."],
    '>': [".X...", "..X..", "...X.", "....X", "...X.", "..X..", ".X..."],
    '?': [".XXX.", "X...X", "....X", "...X.", "..X..", ".....", "..X.."],
    '@': [".XXX.", "X...X", "X.XXX", "X.X.X", "X.XX.", "X....", ".XXX."],
    'A': [".XXX.", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"],
    'B': ["XXXX.", "X...X", "X...X", "XXXX.", "X...X", "X...X", "XXXX."],
    'C': [".XXX.", "X...X", "X....", "X....", "X....", "X...X", ".XXX."],
    'D': ["XXXX.", "X...X", "X...X", "X...X", "X...X", "X...X", "XXXX."],
    'E': ["XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "XXXXX"],
    'F': ["XXXXX", "X....", "X....", "XXXX.", "X....", "X....", "X...."],
    'G': [".XXX.", "X...X", "X....", "X.XXX", "X...X", "X...X", ".XXXX"],
    'H': ["X...X", "X...X", "X...X", "XXXXX", "X...X", "X...X", "X...X"],
    'I': [".XXX.", "..X..", "..X..", "..X..", "..X..", "..X..", ".XXX."],
    'J': ["..XXX", "...X.", "...X.", "...X.", "...X.", "X..X.", ".XX.."],
    'K': ["X...X", "X..X.", "X.X..", "XX...", "X.X..", "X..X.", "X...X"],
    'L': ["X....", "X....", "X....", "X....", "X....", "X....", "XXXXX"],
    'M': ["X...X", "XX.XX", "X.X.X", "X.X.X", "X...X", "X...X", "X...X"],
    'N': ["X...X", "X...X", "XX..X", "X.X.X", "X..XX", "X...X", "X...X"],
    'O': [".XXX.", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."],
    'P': ["XXXX.", "X...X", "X...X", "XXXX.", "X....", "X....", "X...."],
    'Q': [".XXX.", "X...X", "X...X", "X...X", "X.X.X", "X..X.", ".XX.X"],
    'R': ["XXXX.", "X...X", "X...X", "XXXX.", "X.X..", "X..X.", "X...X"],
    'S': [".XXXX", "X....", "X....", ".XXX.", "....X", "....X", "XXXX."],
    'T': ["XXXXX", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."],
    'U': ["X...X", "X...X", "X...X", "X...X", "X...X", "X...X", ".XXX."],
    'V': ["X...X", "X...X", "X...X", "X...X", "X...X", ".X.X.", "..X.."],
    'W': ["X...X", "X...X", "X...X", "X.X.X", "X.X.X", "XX.XX", "X...X"],
    'X': ["X...X", "X...X", ".X.X.", "..X..", ".X.X.", "X...X", "X...X"],
    'Y': ["X...X", "X...X", ".X.X.", "..X..", "..X..", "..X..", "..X.."],
    'Z': ["XXXXX", "....X", "...X.", "..X..", ".X...", "X....", "XXXXX"],
    '[': [".XXX.", ".X...", ".X...", ".X...", ".X...", ".X...", ".XXX."],
    '\\': ["X....", ".X...", "..X..", "...X.", "....X", ".....", "....."],
    ']': [".XXX.", "...X.", "...X.", "...X.", "...X.", "...X.", ".XXX."],
    '^': ["..X..", ".X.X.", "X...X", ".....", ".....", ".....", "....."],
    '_': [".....", ".....", ".....", ".....", ".....", ".....", "XXXXX"],
    '`': [".X...", "..X..", "...X.", ".....", ".....", ".....", "....."],
    'a': [".....", ".....", ".XXX.", "....X", ".XXXX", "X...X", ".XXXX"],
    'b': ["X....", "X....", "XXXX.", "X...X", "X...X", "X...X", "XXXX."],
    'c': [".....", ".....", ".XXXX", "X....", "X....", "X....", ".XXXX"],
    'd': ["....X", "....X", ".XXXX", "X...X", "X...X", "X...X", ".XXXX"],
    'e': [".....", ".....", ".XXX.", "X...X", "XXXXX", "X....", ".XXX."],
    'f': ["..XX.", ".X..X", ".X...", "XXX..", ".X...", ".X...", ".X..."],
    'g': [".....", ".XXXX", "X...X", "X...X", ".XXXX", "....X", ".XXX."],
    'h': ["X....", "X....", "XXXX.", "X...X", "X...X", "X...X", "X...X"],
    'i': ["..X..", ".....", ".XX..", "..X..", "..X..", "..X..", ".XXX."],
    'j': ["...X.", ".....", "..XX.", "...X.", "...X.", "X..X.", ".XX.."],
    'k': ["X....", "X....", "X..X.", "X.X..", "XX...", "X.X..", "X..X."],
    'l': [".XX..", "..X..", "..X..", "..X..", "..X..", "..X..", ".XXX."],
    'm': [".....", ".....", "XX.X.", "X.X.X", "X.X.X", "X.X.X", "X.X.X"],
    'n': [".....", ".....", "XXXX.", "X...X", "X...X", "X...X", "X...X"],
    'o': [".....", ".....", ".XXX.", "X...X", "X...X", "X...X", ".XXX."],
    'p': [".....", "XXXX.", "X...X", "X...X", "XXXX.", "X....", "X...."],
    'q': [".....", ".XXXX", "X...X", "X...X", ".XXXX", "....X", "....X"],
    'r': [".....", ".....", "X.XX.", "XX..X", "X....", "X....", "X...."],
    's': [".....", ".....", ".XXXX", "X....", ".XXX.", "....X", "XXXX."],
    't': [".X...", ".X...", "XXX..", ".X...", ".X...", ".X..X", "..XX."],
    'u': [".....", ".....", "X...X", "X...X", "X...X", "X..XX", ".XX.X"],
    'v': [".....", ".....", "X...X", "X...X", "X...X", ".X.X.", "..X.."],
    'w': [".....", ".....", "X...X", "X...X", "X.X.X", "X.X.X", ".X.X."],
    'x': [".....", ".....", "X...X", ".X.X.", "..X..", ".X.X.", "X...X"],
    'y': [".....", "X...X", "X...X", "X...X", ".XXXX", "....X", ".XXX."],
    'z': [".....", ".....", "XXXXX", "...X.", "..X..", ".X...", "XXXXX"],
    '{': ["...XX", "..X..", "..X..", ".X...", "..X..", "..X..", "...XX"],
    '|': ["..X..", "..X..", "..X..", "..X..", "..X..", "..X..", "..X.."],
    '}': ["XX...", "..X..", "..X..", "...X.", "..X..", "..X..", "XX..."],
    '~': [".....", ".....", ".XX.X", "X..X.", ".....", ".....", "....."],
}

FONT_ROWS = 7
RANGE_BEGIN = 33
RANGE_END = 126

# 1024 so the larger integer font scales (Font.36/Font.48, for high-DPI
# titles) fit alongside Font.9/Font.24 and the sprites. The runtime normalizes
# every metric by the actual texture size (RenderSystem::getTextureSize), so
# the size is not baked into any consumer.
TEXTURE_SIZE = 1024

#: (glyphDataIndex, pixel scale) - Font.9 = HUD text, Font.24 = titles,
#: Font.36 / Font.48 = the crisp integer upscales high-DPI screens pick so big
#: text stays sharp when the UI scales (point-filtered, so scale must be whole)
FONTS = [(9, 2), (24, 4), (36, 6), (48, 8)]

TEXT_COLOUR = (255, 255, 255, 255)


# ---------------------------------------------------------------------------
# tiny RGBA canvas + PNG writer (stdlib zlib/struct only)
# ---------------------------------------------------------------------------

class Canvas:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.pixels = bytearray(width * height * 4)  # transparent black

    def put(self, x, y, rgba):
        if 0 <= x < self.width and 0 <= y < self.height:
            i = (y * self.width + x) * 4
            self.pixels[i:i + 4] = bytes(rgba)

    def get(self, x, y):
        i = (y * self.width + x) * 4
        return tuple(self.pixels[i:i + 4])

    def fill_rect(self, x, y, w, h, rgba):
        for yy in range(y, y + h):
            for xx in range(x, x + w):
                self.put(xx, yy, rgba)

    def write_png(self, path):
        raw = bytearray()
        stride = self.width * 4
        for y in range(self.height):
            raw.append(0)  # filter type None
            raw.extend(self.pixels[y * stride:(y + 1) * stride])

        def chunk(tag, payload):
            data = tag + payload
            return (struct.pack(">I", len(payload)) + data +
                    struct.pack(">I", zlib.crc32(data) & 0xFFFFFFFF))

        ihdr = struct.pack(">IIBBBBB", self.width, self.height, 8, 6, 0, 0, 0)
        png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
               chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
               chunk(b"IEND", b""))
        with open(path, "wb") as handle:
            handle.write(png)


def draw_rounded_rect(canvas, x, y, w, h, radius, fill, border,
                      border_width=1, fill_gradient=None):
    """Rounded rect with a border. fill_gradient: optional second fill colour
    for a vertical top-to-bottom gradient."""
    def corner_outside(px, py):
        # distance test against the four corner circles
        for cx, cy in ((x + radius, y + radius),
                       (x + w - 1 - radius, y + radius),
                       (x + radius, y + h - 1 - radius),
                       (x + w - 1 - radius, y + h - 1 - radius)):
            pass
        if px < x + radius and py < y + radius:
            cx, cy = x + radius, y + radius
        elif px >= x + w - radius and py < y + radius:
            cx, cy = x + w - 1 - radius, y + radius
        elif px < x + radius and py >= y + h - radius:
            cx, cy = x + radius, y + h - 1 - radius
        elif px >= x + w - radius and py >= y + h - radius:
            cx, cy = x + w - 1 - radius, y + h - 1 - radius
        else:
            return False, False
        d2 = (px - cx) ** 2 + (py - cy) ** 2
        return d2 > radius * radius, d2 > (radius - border_width) ** 2

    for py in range(y, y + h):
        for px in range(x, x + w):
            outside, on_border = corner_outside(px, py)
            if outside:
                continue
            edge = (px - x < border_width or x + w - 1 - px < border_width or
                    py - y < border_width or y + h - 1 - py < border_width)
            if edge or on_border:
                canvas.put(px, py, border)
            else:
                colour = fill
                if fill_gradient is not None and h > 2:
                    t = (py - y) / (h - 1)
                    colour = tuple(int(a + (b - a) * t)
                                   for a, b in zip(fill, fill_gradient))
                canvas.put(px, py, colour)


# ---------------------------------------------------------------------------
# atlas building
# ---------------------------------------------------------------------------

def glyph_columns(rows):
    """Trimmed column range [first, last] of a 5x7 glyph ('|' -> 2..2)."""
    used = [c for c in range(5) if any(row[c] == 'X' for row in rows)]
    if not used:
        raise ValueError("empty glyph")
    return used[0], used[-1]


def build_atlas(out_dir, atlas_name):
    canvas = Canvas(TEXTURE_SIZE, TEXTURE_SIZE)
    ogui = []
    occupied = []          # (x, y, w, h, what) for overlap validation

    def claim(x, y, w, h, what):
        assert x >= 0 and y >= 0 and x + w <= TEXTURE_SIZE and \
            y + h <= TEXTURE_SIZE, f"{what} out of texture bounds"
        occupied.append((x, y, w, h, what))

    # --- whitepixel: 4x4 opaque white block, bottom-right corner ----------
    wp_block = 4
    wp_x = TEXTURE_SIZE - wp_block
    wp_y = TEXTURE_SIZE - wp_block
    canvas.fill_rect(wp_x, wp_y, wp_block, wp_block, (255, 255, 255, 255))
    claim(wp_x, wp_y, wp_block, wp_block, "whitepixel")
    white_x, white_y = TEXTURE_SIZE - 2, TEXTURE_SIZE - 2  # inside the block

    ogui.append("# generated by Util/make_gui_atlas.py - do not edit")
    ogui.append("[Texture]")
    ogui.append(f"file {atlas_name}.png")
    ogui.append(f"whitepixel {white_x} {white_y}")

    # --- fonts --------------------------------------------------------------
    cursor_y = 0
    for glyph_index, scale in FONTS:
        cell_h = FONT_ROWS * scale
        line_height = (FONT_ROWS + 2) * scale
        baseline = FONT_ROWS * scale          # bottom row = baseline
        space_length = 4 * scale
        mono_width = 6 * scale
        pad = 2  # gap between glyph cells so bilinear/copy never bleeds

        ogui.append("")
        ogui.append(f"[Font.{glyph_index}]")
        ogui.append("offset 0 0")
        ogui.append(f"lineheight {line_height}")
        ogui.append(f"baseline {baseline}")
        ogui.append(f"spacelength {space_length}")
        ogui.append(f"monowidth {mono_width}")
        ogui.append(f"range {RANGE_BEGIN} {RANGE_END}")

        x = 0
        y = cursor_y
        row_h = cell_h + pad
        for code in range(RANGE_BEGIN, RANGE_END + 1):
            rows = FONT_5X7[chr(code)]
            first, last = glyph_columns(rows)
            width = (last - first + 1) * scale
            if x + width > TEXTURE_SIZE - wp_block - pad:
                x = 0
                y += row_h
            # rasterize (trimmed columns, full height)
            for ry in range(FONT_ROWS):
                for rx in range(first, last + 1):
                    if rows[ry][rx] == 'X':
                        canvas.fill_rect(x + (rx - first) * scale,
                                         y + ry * scale, scale, scale,
                                         TEXT_COLOUR)
            claim(x, y, width, cell_h, f"glyph {glyph_index}/{code}")
            advance = width + scale  # 1px letter gap baked into the advance
            ogui.append(f"glyph_{code} {x} {y} {width} {cell_h} {advance}")
            x += width + pad
        cursor_y = y + row_h + pad

    # --- UI sprites ----------------------------------------------------------
    ogui.append("")
    ogui.append("[Sprites]")

    sprite_defs = []

    def add_sprite(name, x, y, w, h, draw, slice=None):
        # slice = (left, right, top, bottom) nine-slice border insets in sprite
        # pixels (None = a plain stretched sprite). The rounded-corner sprites
        # carry insets so a resized panel/button keeps crisp corners.
        draw(x, y, w, h)
        claim(x, y, w, h, f"sprite {name}")
        sprite_defs.append((name, x, y, w, h, slice))

    white = (240, 244, 248, 255)
    panel_fill = (22, 26, 34, 208)
    button_fill = (54, 86, 140, 255)
    button_over = (82, 122, 188, 255)
    button_down = (36, 58, 96, 255)
    button_disabled = (72, 74, 80, 255)
    bar_frame_fill = (18, 20, 26, 224)
    bar_fill_top = (110, 214, 92, 255)
    bar_fill_bottom = (52, 140, 44, 255)

    y0 = cursor_y + 2
    add_sprite("panel", 0, y0, 48, 48, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 6, panel_fill, white),
               slice=(12, 12, 12, 12))
    add_sprite("button", 52, y0, 64, 24, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 4, button_fill, white),
               slice=(8, 8, 8, 8))
    add_sprite("button_over", 120, y0, 64, 24, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 4, button_over, white),
               slice=(8, 8, 8, 8))
    add_sprite("button_down", 188, y0, 64, 24, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 4, button_down, white),
               slice=(8, 8, 8, 8))
    add_sprite("button_disabled", 256, y0, 64, 24, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 4, button_disabled,
                                 (140, 140, 140, 255)),
               slice=(8, 8, 8, 8))
    add_sprite("progressbar", 0, y0 + 52, 96, 16, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 3, bar_frame_fill, white),
               slice=(6, 6, 6, 6))
    add_sprite("progressbar_bar", 100, y0 + 52, 88, 10, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 2, bar_fill_top,
                                 (200, 255, 190, 255),
                                 fill_gradient=bar_fill_bottom))

    # settings widgets: the checkbox toggle states and the select-menu /
    # slider field + arrows. Unused by the HUD games but present so a settings
    # screen (GuiCheckBox / GuiSelectMenu / GuiSlider) has art.
    checkbox_off_fill = (40, 46, 58, 255)
    checkbox_on_fill = (110, 214, 92, 255)
    add_sprite("checkbox_off", 0, y0 + 72, 28, 28, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 4, checkbox_off_fill,
                                 white))
    add_sprite("checkbox_on", 32, y0 + 72, 28, 28, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 4, checkbox_on_fill,
                                 (200, 255, 190, 255)))
    add_sprite("select_menu_field", 64, y0 + 72, 96, 24, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 4, button_fill, white),
               slice=(8, 8, 8, 8))
    add_sprite("select_menu_field_left", 164, y0 + 72, 16, 24,
               lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 3, button_over, white))
    add_sprite("select_menu_field_right", 184, y0 + 72, 16, 24,
               lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 3, button_over, white))
    # the draggable knob the Slider rides along its track (a lighter accent so
    # it stands out over the field); the Slider also tints the track with it
    slider_pin_fill = (150, 190, 245, 255)
    add_sprite("select_menu_pin", 204, y0 + 72, 20, 24, lambda x, y, w, h:
               draw_rounded_rect(canvas, x, y, w, h, 5, slider_pin_fill, white))

    for name, x, y, w, h, slice in sprite_defs:
        if slice is not None:
            l, r, t, b = slice
            ogui.append(f"{name} {x} {y} {w} {h} {l} {r} {t} {b}")
        else:
            ogui.append(f"{name} {x} {y} {w} {h}")

    # --- write output ---------------------------------------------------------
    png_path = os.path.join(out_dir, f"{atlas_name}.png")
    ogui_path = os.path.join(out_dir, f"{atlas_name}.ogui")
    canvas.write_png(png_path)
    with open(ogui_path, "w", newline="\n") as handle:
        handle.write("\n".join(ogui) + "\n")
    # glyph atlases are pixel-exact: stamp format="none" so the export cook
    # ships the PNG untouched (block compression smears glyph edges); the
    # stamp preserves an existing id and never overrides a chosen format
    orkige_sidecar.stamp_texture_sidecar(png_path)
    return png_path, ogui_path, canvas, occupied


# ---------------------------------------------------------------------------
# self-validation: re-parse the .ogui with UiAtlas' rules
# ---------------------------------------------------------------------------

def parse_ogui(path):
    """Parse like Ogre::ConfigFile(separators=' ', trim=true): first token is
    the key, the rest the value; '#' lines are comments; [x] opens sections.
    Returns {section: [(key, value)]} with sections lowercased (UiAtlas
    lowercases them too)."""
    sections = {"": []}
    current = ""
    with open(path, "r") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#") or line.startswith("@"):
                continue
            if line.startswith("[") and line.endswith("]"):
                current = line[1:-1].lower()
                sections.setdefault(current, [])
                continue
            if " " in line:
                key, value = line.split(" ", 1)
            else:
                key, value = line, ""
            sections[current].append((key, value.strip()))
    return sections


def fail(msg):
    print(f"make_gui_atlas: VALIDATION FAILED - {msg}", file=sys.stderr)
    sys.exit(1)


def validate(png_path, ogui_path, canvas, occupied):
    # occupancy: no two claimed rects may overlap
    grid = bytearray(TEXTURE_SIZE * TEXTURE_SIZE)
    for x, y, w, h, what in occupied:
        for yy in range(y, y + h):
            row = yy * TEXTURE_SIZE
            for xx in range(x, x + w):
                if grid[row + xx]:
                    fail(f"overlap at {xx},{yy} ({what})")
                grid[row + xx] = 1

    sections = parse_ogui(ogui_path)

    # [Texture]
    tex = dict(sections.get("texture", []))
    if tex.get("file") != os.path.basename(png_path):
        fail("texture file entry does not match the png")
    wx, wy = (int(v) for v in tex["whitepixel"].split())
    if canvas.get(wx, wy) != (255, 255, 255, 255):
        fail("whitepixel does not point at an opaque white texel")

    # PNG IHDR must match the canvas (we wrote it, but verify the contract)
    with open(png_path, "rb") as handle:
        header = handle.read(24)
    width, height = struct.unpack(">II", header[16:24])
    if (width, height) != (TEXTURE_SIZE, TEXTURE_SIZE):
        fail("png size mismatch")

    # fonts
    font_sections = [s for s in sections if s.startswith("font.")]
    if len(font_sections) != len(FONTS):
        fail("font section count mismatch")
    for section in font_sections:
        entries = sections[section]
        keys = dict(entries)
        for required in ("offset", "lineheight", "baseline", "spacelength",
                         "monowidth", "range"):
            if required not in keys:
                fail(f"[{section}] missing '{required}'")
        begin, end = (int(v) for v in keys["range"].split())
        if (begin, end) != (RANGE_BEGIN, RANGE_END):
            fail(f"[{section}] unexpected range")
        off_x, off_y = (int(v) for v in keys["offset"].split())
        glyphs = {}
        for key, value in entries:
            if not key.startswith("glyph_"):
                continue
            code = int(key[6:])
            fields = value.split()
            if len(fields) != 5:
                fail(f"[{section}] glyph_{code}: want x y w h advance")
            x, y, w, h, advance = (int(v) for v in fields)
            x += off_x
            y += off_y
            if w <= 0 or h <= 0 or advance <= 0:
                fail(f"[{section}] glyph_{code}: degenerate")
            if x < 0 or y < 0 or x + w > width or y + h > height:
                fail(f"[{section}] glyph_{code}: out of bounds")
            # the glyph rect must contain at least one text-coloured pixel
            if not any(canvas.get(px, py)[3] != 0
                       for py in range(y, y + h) for px in range(x, x + w)):
                fail(f"[{section}] glyph_{code}: empty rect")
            glyphs[code] = (w, h)
        # UiAtlas creates a glyph slot for EVERY code in the range; missing
        # entries stay zero-sized and render nothing - require full coverage
        for code in range(begin, end + 1):
            if code not in glyphs:
                fail(f"[{section}] glyph_{code} missing from range")

    # sprites (parsed as unsigned ints by UiAtlas - must be plain ints)
    sprites = dict(sections.get("sprites", []))
    for required in ("panel", "button", "button_over", "button_down",
                     "button_disabled", "progressbar", "progressbar_bar"):
        if required not in sprites:
            fail(f"sprite '{required}' missing")
    for name, value in sprites.items():
        fields = value.split()
        # a sprite is "x y w h" or, with nine-slice insets, "x y w h l r t b"
        if len(fields) not in (4, 8):
            fail(f"sprite {name}: want x y w h [l r t b]")
        ints = [int(v) for v in fields]
        x, y, w, h = ints[:4]
        if w <= 0 or h <= 0 or x + w > width or y + h > height:
            fail(f"sprite {name}: out of bounds")
        if len(ints) == 8:
            sl, sr, st, sb = ints[4:]
            if min(sl, sr, st, sb) < 0:
                fail(f"sprite {name}: negative slice inset")
            if sl + sr >= w or st + sb >= h:
                fail(f"sprite {name}: slice insets exceed the sprite size")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    out_dir = sys.argv[1]
    atlas_name = sys.argv[2] if len(sys.argv) > 2 else "gui_default"
    os.makedirs(out_dir, exist_ok=True)
    png_path, ogui_path, canvas, occupied = build_atlas(out_dir, atlas_name)
    validate(png_path, ogui_path, canvas, occupied)
    print(f"make_gui_atlas: wrote {ogui_path} + {png_path} (validated)")


if __name__ == "__main__":
    main()
