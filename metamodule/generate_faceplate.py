#!/usr/bin/env python3
"""
Generate MetaModule faceplate PNGs for Bemushroomed modules.

MetaModule faceplates must be 240px high.
Trigonomicon: 10HP = 50.8mm wide -> 95px wide.
SlideWyrm: 12HP = 60.96mm wide -> ~114px wide.

Usage:
    pip install Pillow
    python generate_faceplate.py

Output: assets/trigonomicon.png, assets/SlideWyrm.png
"""

import os
import sys

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("ERROR: Pillow is required. Install with: pip install Pillow")
    sys.exit(1)

# MetaModule faceplate dimensions
MODULE_HEIGHT_MM = 128.5
TRIG_WIDTH_MM = 50.8
SW_WIDTH_MM = 60.96
AMEN_WIDTH_MM = 121.92  # 24HP
PHASEON_WIDTH_MM = 101.6  # 20HP
XENOSTASIS_WIDTH_MM = 81.28  # 16HP
HEIGHT = 240
PX_PER_MM = HEIGHT / MODULE_HEIGHT_MM  # ~1.8677
TRIG_WIDTH = int(TRIG_WIDTH_MM * PX_PER_MM)  # ~95 px
SW_WIDTH = int(SW_WIDTH_MM * PX_PER_MM)  # ~114 px
AMEN_WIDTH = int(AMEN_WIDTH_MM * PX_PER_MM)  # ~228 px
PHASEON_WIDTH = int(PHASEON_WIDTH_MM * PX_PER_MM)  # ~189 px
XENOSTASIS_WIDTH = int(XENOSTASIS_WIDTH_MM * PX_PER_MM)  # ~152 px

# Colors
BG_COLOR = (0x2a, 0x2b, 0x48)
NEON_GREEN = (0xa7, 0xff, 0xc4)
DIM_GREEN = (0x7d, 0xbf, 0x93)
BLACK = (0x08, 0x08, 0x08)


def mm_to_px(mm_x, mm_y):
    """Convert mm coordinates to pixel coordinates."""
    return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))


def mm(val):
    """Convert a single mm value to pixels."""
    return int(val * PX_PER_MM)


def load_fonts(base_dir):
    font_path_title = os.path.join(base_dir, '..', 'res', 'CinzelDecorative-Bold.ttf')
    font_path_label = os.path.join(base_dir, '..', 'res', 'Rajdhani-Bold.ttf')
    try:
        fonts = {
            'title': ImageFont.truetype(font_path_title, max(5, mm(2.8))),
            'subtitle': ImageFont.truetype(font_path_label, max(5, mm(2.2))),
            'section': ImageFont.truetype(font_path_label, max(5, mm(2.5))),
            'label': ImageFont.truetype(font_path_label, max(5, mm(2.5))),
            'small': ImageFont.truetype(font_path_label, max(5, mm(2.0))),
            'drum': ImageFont.truetype(font_path_label, max(5, mm(3.0))),
            'output': ImageFont.truetype(font_path_label, max(5, mm(3.5))),
        }
    except (IOError, OSError):
        print("WARNING: Could not load custom fonts, using default")
        default = ImageFont.load_default()
        fonts = {k: default for k in ['title', 'subtitle', 'section', 'label', 'small', 'drum', 'output']}
    return fonts


def draw_border(draw, width, lines_y=None):
    """Draw green border and optional accent lines."""
    border_inset = mm(1.5)
    draw.rounded_rectangle(
        [border_inset, border_inset, width - border_inset, HEIGHT - border_inset],
        radius=mm(2), outline=NEON_GREEN, width=1
    )
    if lines_y:
        line_x1 = mm(4)
        line_x2 = width - mm(4)
        for y in lines_y:
            draw.line([line_x1, mm(y), line_x2, mm(y)], fill=NEON_GREEN, width=1)


def generate_trigonomicon(fonts, out_dir):
    img = Image.new('RGB', (TRIG_WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    draw_border(draw, TRIG_WIDTH, [65, 119])

    # Title
    draw.text(mm_to_px(25.4, 7), "TRIGONOMICON", fill=NEON_GREEN, font=fonts['title'], anchor="mm")
    draw.text(mm_to_px(25.4, 13), "Pattern Select", fill=DIM_GREEN, font=fonts['section'], anchor="mm")

    # LCD area
    lcd_x1, lcd_y1 = mm_to_px(2.9, 31)
    lcd_x2, lcd_y2 = mm_to_px(47.9, 37)
    draw.rounded_rectangle([lcd_x1, lcd_y1, lcd_x2, lcd_y2], radius=mm(0.8), fill=BLACK, outline=NEON_GREEN, width=1)

    # Clock and Reset
    draw.text(mm_to_px(12.7, 39), "Clock & Reset", fill=DIM_GREEN, font=fonts['section'], anchor="mm")
    draw.text(mm_to_px(12.7, 43), "CLK", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(mm_to_px(12.7, 53), "RST", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Mutate
    draw.text(mm_to_px(38.1, 39), "Mutate", fill=DIM_GREEN, font=fonts['section'], anchor="mm")
    draw.text(mm_to_px(38.1, 43), "AMT", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(mm_to_px(38.1, 53), "CV", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Vertical OUTPUTS
    for i, ch in enumerate("OUTPUTS"):
        draw.text(mm_to_px(43, 74 + i * 5.5), ch, fill=DIM_GREEN, font=fonts['output'], anchor="mm")

    # Drum labels
    for i, lbl in enumerate(["KCK", "SN1", "SN2", "CHH", "OHH", "BAS"]):
        y = 72.0 + i * 10.0
        draw.text(mm_to_px(11, y - 2.5), lbl, fill=NEON_GREEN, font=fonts['drum'], anchor="mm")

    out_path = os.path.join(out_dir, 'trigonomicon.png')
    img.save(out_path)
    print(f"Trigonomicon faceplate saved: {out_path} ({TRIG_WIDTH}x{HEIGHT})")


def generate_slidewyrm(fonts, out_dir):
    img = Image.new('RGB', (SW_WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    def sw_px(mm_x, mm_y):
        return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))

    # Border only — no accent lines
    draw_border(draw, SW_WIDTH)

    # Layout constants (matching VCV widget)
    centerX = 30.48
    leftX = 13.0
    rightX = 48.0

    # Title
    draw.text(sw_px(centerX, 5), "SLIDEWYRM", fill=NEON_GREEN, font=fonts['title'], anchor="mm")
    draw.text(sw_px(centerX, 9.5), "303 Pattern Generator", fill=DIM_GREEN, font=fonts['subtitle'], anchor="mm")

    # Row 1: Density + Scale
    draw.text(sw_px(leftX, 17.5), "DENSITY", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(sw_px(rightX, 17.5), "SCALE", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Row 2: Root + Octave
    draw.text(sw_px(leftX, 28.5), "ROOT", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(sw_px(rightX, 28.5), "OCTAVE", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Row 3: Seed + Regen
    draw.text(sw_px(leftX, 40.5), "SEED", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(sw_px(rightX, 40.5), "REGEN", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Row 3b: Slide Amount + Gate Length
    draw.text(sw_px(12, 56), "SLIDE AMOUNT", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(38, 56), "GATE LENGTH", fill=NEON_GREEN, font=fonts['small'], anchor="mm")

    # Row 4: Switches — LOCK, STEPS, GATE, ACC, ACC.STEP
    draw.text(sw_px(9, 73.5), "LOCK", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(22, 73.5), "STEPS", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(30.5, 73.5), "GATE", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(39, 73.5), "ACC", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(52, 73.5), "ACC.STEP", fill=NEON_GREEN, font=fonts['small'], anchor="mm")

    # Indicator labels
    draw.text(sw_px(10, 88.5), "SLD", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(21, 88.5), "ACC", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(40, 88.5), "UP", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(51, 88.5), "DN", fill=DIM_GREEN, font=fonts['small'], anchor="mm")

    # Input labels
    draw.text(sw_px(9, 98), "CLK", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(20, 98), "RST", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(30.48, 98), "TRN", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(41, 98), "DNS", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(sw_px(52, 98), "REG", fill=NEON_GREEN, font=fonts['small'], anchor="mm")

    # Output labels
    draw.text(sw_px(11, 120), "PITCH", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(sw_px(30.48, 120), "GATE", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(sw_px(50, 120), "ACC", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    out_path = os.path.join(out_dir, 'SlideWyrm.png')
    img.save(out_path)
    print(f"SlideWyrm faceplate saved: {out_path} ({SW_WIDTH}x{HEIGHT})")


def generate_phasewarpeddrums(fonts, out_dir):
    # PhaseWarpedDrums is 24HP = 121.92mm wide
    PWD_WIDTH_MM = 121.92
    PWD_WIDTH = int(PWD_WIDTH_MM * PX_PER_MM)  # ~228px
    
    img = Image.new('RGB', (PWD_WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)
    
    def pwd_px(mm_x, mm_y):
        return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))
    
    # Border only
    draw_border(draw, PWD_WIDTH)
    
    # Title (20% larger = 14.4pt)
    draw.text(pwd_px(60.96, 5.5), "SEPTAGON", fill=NEON_GREEN, font=fonts['title'], anchor="mm")
    
    # Section headers (doubled - use drum font)
    draw.text(pwd_px(30.5, 16), "PATTERN", fill=DIM_GREEN, font=fonts['drum'], anchor="mm")
    draw.text(pwd_px(91.5, 16), "RHYTHM", fill=DIM_GREEN, font=fonts['drum'], anchor="mm")
    
    # Knob labels row 1
    draw.text(pwd_px(15, 26), "CHAOS", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(pwd_px(46, 26), "DENSITY", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(pwd_px(76, 26), "SWING", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(pwd_px(107, 26), "TENSION", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    
    # Knob labels row 2
    draw.text(pwd_px(15, 52), "EVOLVE", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(pwd_px(46, 52), "GROUPING", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(pwd_px(60.96, 55), "GEN", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    
    # Switch labels
    draw.text(pwd_px(76, 69), "PATTERN LENGTH", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    draw.text(pwd_px(107, 69), "REGEN", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    
    # CV section
    draw.text(pwd_px(60.96, 82), "CV INPUTS", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    cv_labels = [(15, "CHAOS"), (30.5, "DENS"), (46, "EVOL"), 
                 (61.5, "SWING"), (76, "TENS"), (91.5, "GEN"), (107, "RESET")]
    for x, lbl in cv_labels:
        draw.text(pwd_px(x, 86), lbl, fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    
    # Clock
    draw.text(pwd_px(60.96, 98), "CLOCK", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    
    # Drum outputs section
    draw.text(pwd_px(60.96, 109), "DRUM OUTPUTS", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    drum_labels = [(15, "KICK"), (36.5, "SNARE"), (58, "GHOST"), 
                   (79.5, "C.HAT"), (101, "O.HAT")]
    for x, lbl in drum_labels:
        draw.text(pwd_px(x, 113), lbl, fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    
    # G/V labels
    gv_labels = [(11, "G"), (19, "V"), (33, "G"), (40, "V"), (54.5, "G"), 
                 (61.5, "V"), (76, "G"), (83, "V"), (97.5, "G"), (104.5, "V")]
    for x, lbl in gv_labels:
        draw.text(pwd_px(x, 125), lbl, fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    
    # Utility section
    draw.text(pwd_px(60.96, 136), "UTILITY", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    util_labels = [(15, "GRP1"), (30.5, "GRP2"), (46, "GRP3"), 
                   (76, "PHASE"), (107, "ACCENT")]
    for x, lbl in util_labels:
        draw.text(pwd_px(x, 145), lbl, fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    
    out_path = os.path.join(out_dir, 'Septagon.png')
    img.save(out_path)
    print(f"Septagon faceplate saved: {out_path} ({PWD_WIDTH}x{HEIGHT})")


def generate_minimalith(fonts, out_dir):
    # Minimalith is 16HP = 81.28mm wide (matches the VCV port layout)
    ML_WIDTH_MM = 81.28
    ML_WIDTH = int(ML_WIDTH_MM * PX_PER_MM)  # ~152px

    img = Image.new('RGB', (ML_WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    def ml_px(mm_x, mm_y):
        return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))

    # Border
    draw_border(draw, ML_WIDTH)

    # Layout constants (matching the VCV widget's mm coordinates)
    centerX = 40.64

    # 5-column grid across the panel (used for button row and CV inputs)
    gridLeft = 10.0
    gridRight = 71.28
    gridStep = (gridRight - gridLeft) / 4.0
    x0 = gridLeft + gridStep * 0.0
    x1 = gridLeft + gridStep * 1.0
    x2 = gridLeft + gridStep * 2.0
    x3 = gridLeft + gridStep * 3.0
    x4 = gridLeft + gridStep * 4.0

    # 4-column grid (used for FM editor trims)
    trimLeft = 16.0
    trimRight = 65.28
    trimStep = (trimRight - trimLeft) / 3.0
    t0 = trimLeft + trimStep * 0.0
    t1 = trimLeft + trimStep * 1.0
    t2 = trimLeft + trimStep * 2.0
    t3 = trimLeft + trimStep * 3.0

    # Title: "M I N I M A L I T H" (spaced) — ~25% larger on MetaModule
    title_font = fonts['title']
    try:
        base_dir = os.path.dirname(out_dir)
        font_path_title = os.path.join(base_dir, '..', 'res', 'CinzelDecorative-Bold.ttf')
        title_font = ImageFont.truetype(font_path_title, int(getattr(fonts['title'], 'size', mm(2.8)) * 1.25))
    except Exception:
        title_font = fonts['title']

    draw.text(ml_px(centerX, 5.5), "M I N I M A L I T H", fill=NEON_GREEN, font=title_font, anchor="mm")
    # Subtitle intentionally removed on MetaModule to keep clear space above the screen.

    # Display area (dark rect)
    # Expanded ~20% vs the VCV placement to avoid text looking clipped on MetaModule.
    disp_x1, disp_y1 = ml_px(11.54, 16.64)
    disp_x2, disp_y2 = ml_px(11.54 + 58.20, 16.64 + 16.80)
    draw.rounded_rectangle([disp_x1, disp_y1, disp_x2, disp_y2],
                           radius=mm(0.8), fill=BLACK, outline=NEON_GREEN, width=1)

    # Section title
    draw.text(ml_px(centerX, 34.08), "P R E S E T", fill=NEON_GREEN, font=fonts['section'], anchor="mm")

    # Top row labels (LOAD / PRESET / VOLUME / PANIC)
    topRowY = 44.04
    topLabelY = topRowY + 3.5
    draw.text(ml_px(x0, topLabelY), "LOAD", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(x2, topLabelY), "PRESET", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(x3, topLabelY), "VOLUME", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(x4, topLabelY), "PANIC", fill=NEON_GREEN, font=fonts['small'], anchor="mm")

    # FM editor trims (these are the "trimpots" you asked for)
    draw.text(ml_px(centerX, 54.24), "F M  E D I T O R", fill=NEON_GREEN, font=fonts['section'], anchor="mm")
    fmTrimY = 64.60
    draw.text(ml_px(t0, fmTrimY + 4.5), "ALGO", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(t1, fmTrimY + 4.5), "EVO", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(t2, fmTrimY + 4.5), "IM SCAN", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(t3, fmTrimY + 4.5), "DRIFT", fill=NEON_GREEN, font=fonts['small'], anchor="mm")

    # CV inputs
    draw.text(ml_px(centerX, 75.86), "C V  I N P U T S", fill=NEON_GREEN, font=fonts['section'], anchor="mm")
    cvY = 88.60
    cvLabelY = cvY - 4.0 - 0.68 - 1.02 - 0.68
    draw.text(ml_px(x0, cvLabelY), "ALGO", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(x1, cvLabelY), "EVO", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(x2, cvLabelY), "IM SCAN", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(x3, cvLabelY), "DRIFT", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(ml_px(x4, cvLabelY), "RANDOM", fill=NEON_GREEN, font=fonts['small'], anchor="mm")

    # Gate / Pitch
    draw.text(ml_px(centerX, 101.94), "G A T E   /   P I T C H", fill=NEON_GREEN, font=fonts['section'], anchor="mm")
    gateY = 103.36
    gateLabelY = gateY - 4.0 - 1.02 - 0.68 - 0.34
    draw.text(ml_px(x0, gateLabelY), "GATE", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(ml_px(x4, gateLabelY), "V/OCT", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Audio out
    draw.text(ml_px(centerX, 116.58), "A U D I O  O U T", fill=NEON_GREEN, font=fonts['section'], anchor="mm")
    outY = 112.17
    outLabelY = outY - 4.0 + 4.08
    draw.text(ml_px(x0, outLabelY), "LEFT", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(ml_px(x4, outLabelY), "RIGHT", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Write to assets/res (used by plugin-mm.json) and also to the root for convenience.
    res_dir = os.path.join(out_dir, 'res')
    os.makedirs(res_dir, exist_ok=True)
    out_path_res = os.path.join(res_dir, 'Minimalith.png')
    out_path_root = os.path.join(out_dir, 'Minimalith.png')
    img.save(out_path_res)
    img.save(out_path_root)
    print(f"Minimalith faceplate saved: {out_path_res} ({ML_WIDTH}x{HEIGHT})")


def generate_amenolith(fonts, out_dir):
    img = Image.new('RGB', (AMEN_WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    def am_px(mm_x, mm_y):
        return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))

    draw_border(draw, AMEN_WIDTH)

    # Match the VCV widget's y-offset used to make room for the headline.
    yOff = 2.65

    # Title
    draw.text(am_px(60.0, 7.5), "A M E N O L I T H", fill=NEON_GREEN, font=fonts['title'], anchor="mm")

    # Global row labels (aligned to the current VCV widget layout)
    global_labels = [
        (14.0, "KIT", NEON_GREEN),
        (28.0, "KIT CV", DIM_GREEN),
        (42.0, "ACC", DIM_GREEN),
        (56.0, "DRV CV", DIM_GREEN),
        (66.0, "DRV", DIM_GREEN),
        (76.0, "HUM", DIM_GREEN),
        (86.0, "LYR", DIM_GREEN),
        (102.0, "MIX L", NEON_GREEN),
        (112.0, "MIX R", NEON_GREEN),
    ]
    for x, txt, col in global_labels:
        yy = 13.0 + yOff
        if txt == "KIT":
            yy = 13.0 + yOff - 0.79
        draw.text(am_px(x, yy), txt, fill=col, font=fonts['small'], anchor="mm")

    # Column headers
    col_headers = [
        (16.0, "TRIG"),
        (28.0, "VEL"),
        (40.0, "TUNE"),
        (56.0, "PAN"),
        (70.0, "LVL"),
        (84.0, "SEMI"),
        (98.0, "LEN"),
        (112.0, "OUT"),
    ]
    for x, txt in col_headers:
        draw.text(am_px(x, 31.0 + yOff), txt, fill=DIM_GREEN, font=fonts['small'], anchor="mm")

    # Instrument row labels
    rowY0 = 40.0 + yOff
    rowDy = 18.0
    for i, nm in enumerate(["BD", "SN", "SN2", "CH", "OH"]):
        yy = rowY0 + rowDy * i
        draw.text(am_px(3.5, yy), nm, fill=NEON_GREEN, font=fonts['label'], anchor="lm")

    # Write to assets/res (used by some packaging flows) and also to the root.
    res_dir = os.path.join(out_dir, 'res')
    os.makedirs(res_dir, exist_ok=True)
    out_path_res = os.path.join(res_dir, 'Amenolith.png')
    out_path_root = os.path.join(out_dir, 'Amenolith.png')
    img.save(out_path_res)
    img.save(out_path_root)
    print(f"Amenolith faceplate saved: {out_path_res} ({AMEN_WIDTH}x{HEIGHT})")


def generate_phaseon(fonts, out_dir):
    # Phaseon is 20HP = 101.6mm wide
    img = Image.new('RGB', (PHASEON_WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    def ph_px(mm_x, mm_y):
        return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))

    draw_border(draw, PHASEON_WIDTH)

    # Title + subtitle
    centerX = PHASEON_WIDTH_MM * 0.5
    draw.text(ph_px(centerX, 6.0), "PHASEON", fill=NEON_GREEN, font=fonts['title'], anchor="mm")
    draw.text(ph_px(centerX, 11.0), "6 Op Wavetable Phase-Mod Synth Voice", fill=DIM_GREEN, font=fonts['small'], anchor="mm")

    # Display area (simple placeholder to match the VCV concept)
    disp_x1, disp_y1 = ph_px(10.0, 16.0)
    disp_x2, disp_y2 = ph_px(PHASEON_WIDTH_MM - 10.0, 28.0)
    draw.rounded_rectangle([disp_x1, disp_y1, disp_x2, disp_y2],
                           radius=mm(0.8), fill=BLACK, outline=NEON_GREEN, width=1)

    # Minimal section labels (CPU-test oriented; not a full faceplate layout)
    draw.text(ph_px(centerX, 34.0), "MACROS", fill=NEON_GREEN, font=fonts['section'], anchor="mm")
    draw.text(ph_px(centerX, 78.0), "CV INPUTS", fill=NEON_GREEN, font=fonts['section'], anchor="mm")
    draw.text(ph_px(centerX, 116.0), "I/O", fill=NEON_GREEN, font=fonts['section'], anchor="mm")

    # Write to assets/res (used by some packaging flows) and also to the root.
    res_dir = os.path.join(out_dir, 'res')
    os.makedirs(res_dir, exist_ok=True)
    out_path_res = os.path.join(res_dir, 'Phaseon.png')
    out_path_root = os.path.join(out_dir, 'Phaseon.png')
    img.save(out_path_res)
    img.save(out_path_root)
    print(f"Phaseon faceplate saved: {out_path_res} ({PHASEON_WIDTH}x{HEIGHT})")


def generate_xenostasis(fonts, out_dir):
    # Xenostasis is 16HP = 81.28mm wide
    img = Image.new('RGB', (XENOSTASIS_WIDTH, HEIGHT), BG_COLOR)
    draw = ImageDraw.Draw(img)

    def xs_px(mm_x, mm_y):
        return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))

    draw_border(draw, XENOSTASIS_WIDTH)

    centerX = XENOSTASIS_WIDTH_MM * 0.5  # 40.64

    # Title + subtitle
    draw.text(xs_px(centerX, 5.5), "X E N O S T A S I S", fill=NEON_GREEN, font=fonts['title'], anchor="mm")
    draw.text(xs_px(centerX, 10.5), "hybrid spectral organism", fill=DIM_GREEN, font=fonts['small'], anchor="mm")

    # Row 2: PITCH (center) + TABLE (right)
    draw.text(xs_px(centerX, 18.0), "PITCH", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(xs_px(65.0, 18.0), "TABLE", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Row 3: CHAOS, STABILITY, HOMEOSTASIS
    draw.text(xs_px(14.0, 36.0), "CHAOS", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(xs_px(centerX, 36.0), "STABILITY", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(xs_px(67.0, 36.0), "HOMEO", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Row 4: CROSS, DENSITY + lights
    draw.text(xs_px(16.0, 52.0), "CROSS", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(xs_px(40.0, 52.0), "DENSITY", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(xs_px(58.0, 53.0), "NRG", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    draw.text(xs_px(68.0, 53.0), "STM", fill=DIM_GREEN, font=fonts['small'], anchor="mm")

    # Row 5: CV inputs
    draw.text(xs_px(11.0, 77.0), "V/OCT", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(xs_px(27.0, 77.0), "CLK", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(xs_px(47.0, 77.0), "CHAOS", fill=NEON_GREEN, font=fonts['small'], anchor="mm")
    draw.text(xs_px(67.0, 77.0), "CROSS", fill=NEON_GREEN, font=fonts['small'], anchor="mm")

    # Row 6: Outputs
    draw.text(xs_px(22.0, 103.0), "LEFT", fill=NEON_GREEN, font=fonts['label'], anchor="mm")
    draw.text(xs_px(59.0, 103.0), "RIGHT", fill=NEON_GREEN, font=fonts['label'], anchor="mm")

    # Write to assets/res and root
    res_dir = os.path.join(out_dir, 'res')
    os.makedirs(res_dir, exist_ok=True)
    out_path_res = os.path.join(res_dir, 'Xenostasis.png')
    out_path_root = os.path.join(out_dir, 'Xenostasis.png')
    img.save(out_path_res)
    img.save(out_path_root)
    print(f"Xenostasis faceplate saved: {out_path_res} ({XENOSTASIS_WIDTH}x{HEIGHT})")


def generate_ferroklast_mm(fonts, out_dir):
    """
    FerroklastMM — 24HP MetaModule variant of Ferroklast.
    6 voices: KICK, SN1, SN2, HH (merged CH/OH), RIDE, CLAP
    Panel: 121.92mm wide x 128.5mm tall -> 227px x 240px

    Uses the original Ferroklast.png scaled to 24HP as the base image
    so the visual style matches the full Ferroklast module.
    """
    FK_MM_WIDTH_MM = 121.92  # 24HP
    FK_MM_WIDTH = int(FK_MM_WIDTH_MM * PX_PER_MM)  # ~227px

    # Load and scale the original Ferroklast panel art.
    # Original is 38HP; we scale uniformly to MetaModule height (240px) which
    # gives 360px wide (38HP), then scale width to 24HP (227px).
    base_dir = os.path.dirname(os.path.abspath(__file__))
    orig_path = os.path.join(base_dir, '..', 'res', 'Ferroklast.png')
    orig = Image.open(orig_path).convert('RGB')
    # Scale to MetaModule height → 360×240, then resize width to 24HP
    mm_height_px = HEIGHT  # 240
    full_w = int(orig.width * mm_height_px / orig.height)  # 38HP in px ≈ 360
    orig_scaled = orig.resize((full_w, mm_height_px), Image.LANCZOS)
    # Crop from the left to get the 24HP portion (left-aligned to match voice layout)
    img = orig_scaled.crop((0, 0, FK_MM_WIDTH, mm_height_px))
    draw = ImageDraw.Draw(img)

    def px(mm_x, mm_y):
        return (int(mm_x * PX_PER_MM), int(mm_y * PX_PER_MM))

    # Border + divider lines at macro/grid boundary and trig/out boundary
    draw_border(draw, FK_MM_WIDTH, lines_y=[43, 91])

    # ── Title ───────────────────────────────────────────────────────────
    draw.text(px(60.96, 5.5), "FERROKLAST", fill=NEON_GREEN, font=fonts['title'], anchor="mm")
    draw.text(px(60.96, 10.0), "MM", fill=DIM_GREEN, font=fonts['drum'], anchor="mm")

    # ── Top macro knob section (y=16..40) ────────────────────────────────
    # 8 macro knobs across, 2 rows — same labels as Rack build minus REVERB
    macro_top_y    = 22.0
    macro_bot_y    = 35.0
    macro_label_top = 16.5
    macro_label_bot = 29.5
    # Spacing: 8 knobs across 110mm centred at 60.96mm
    macro_start_x = 8.5
    macro_step    = 13.4
    top_labels = ["RUMBLE", "PUNCH", "BODY", "COLOR", "SNAP", "NOISE", "TRANS", "MATRL"]
    bot_labels = ["CURVE",  "ATK T", "PTCH", "SN RES","SN CUT","SN DRV","RUIN", "DRIVE"]
    for i in range(8):
        x = macro_start_x + i * macro_step
        draw.text(px(x, macro_label_top), top_labels[i], fill=DIM_GREEN, font=fonts['small'], anchor="mm")
        draw.text(px(x, macro_label_bot), bot_labels[i], fill=DIM_GREEN, font=fonts['small'], anchor="mm")
        # Small dot to indicate knob position
        cx, cy = px(x, macro_top_y)
        draw.ellipse([cx-3, cy-3, cx+3, cy+3], outline=NEON_GREEN, width=1)
        cx, cy = px(x, macro_bot_y)
        draw.ellipse([cx-3, cy-3, cx+3, cy+3], outline=NEON_GREEN, width=1)

    # ── 6 Voice columns (y=44..90) ───────────────────────────────────────
    voice_labels = ["KICK", "SN1", "SN2", "HH", "RIDE", "CLAP"]
    # 6 columns spanning x=10..115, step=21mm
    col_start = 10.5
    col_step  = 20.5
    col_xs = [col_start + i * col_step for i in range(6)]

    row_labels = ["LVL", "TUN", "DEC", "VAR", "TRA", "TTY", "TDC"]
    row_start_y = 54.0
    row_step    = 5.2

    for i, (lbl, x) in enumerate(zip(voice_labels, col_xs)):
        # Voice name headline
        draw.text(px(x, 47.0), lbl, fill=NEON_GREEN, font=fonts['drum'], anchor="mm")
        # 7 trimpot rows
        for r, rl in enumerate(row_labels):
            y = row_start_y + r * row_step
            draw.text(px(x, y), rl, fill=DIM_GREEN, font=fonts['small'], anchor="mm")
            dot_x, dot_y = px(x, y + 2.2)
            draw.ellipse([dot_x-2, dot_y-2, dot_x+2, dot_y+2], outline=NEON_GREEN, width=1)

    # ── Trigger inputs + Audio outputs (y=92..128) ───────────────────────
    trig_y = 99.0
    out_y  = 117.0
    for i, (lbl, x) in enumerate(zip(voice_labels, col_xs)):
        draw.text(px(x, trig_y - 5.0), "TRIG", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
        draw.text(px(x, trig_y), lbl,  fill=NEON_GREEN, font=fonts['label'], anchor="mm")
        # Port circle
        dot_x, dot_y = px(x, trig_y + 3.5)
        draw.ellipse([dot_x-4, dot_y-4, dot_x+4, dot_y+4], outline=NEON_GREEN, width=1)
        draw.text(px(x, out_y - 4.0), "OUT", fill=DIM_GREEN, font=fonts['small'], anchor="mm")
        dot_x, dot_y = px(x, out_y + 1.0)
        draw.ellipse([dot_x-4, dot_y-4, dot_x+4, dot_y+4], fill=NEON_GREEN)

    # ── Right mini CV column (x=118mm) ───────────────────────────────────
    cv_x    = 117.5
    cv_y0   = 48.0
    cv_step = 7.5
    cv_labels = ["ACC", "DEC", "CLR", "BDY", "PCH", "SNP", "VAR", "RMB"]
    for r, lbl in enumerate(cv_labels):
        y = cv_y0 + r * cv_step
        draw.text(px(cv_x, y), lbl, fill=DIM_GREEN, font=fonts['small'], anchor="mm")
        dot_x, dot_y = px(cv_x, y + 2.5)
        draw.ellipse([dot_x-2, dot_y-2, dot_x+2, dot_y+2], outline=NEON_GREEN, width=1)

    # SC/GRV outputs and DUCK block at bottom-right
    draw.text(px(cv_x, cv_y0 + 8 * cv_step + 1.0), "SC",  fill=DIM_GREEN, font=fonts['small'], anchor="mm")
    draw.text(px(cv_x, cv_y0 + 9 * cv_step - 1.0), "GRV", fill=DIM_GREEN, font=fonts['small'], anchor="mm")

    # Write to assets/res and assets root
    res_dir = os.path.join(out_dir, 'res')
    os.makedirs(res_dir, exist_ok=True)
    out_path_res  = os.path.join(res_dir, 'FerroklastMM.png')
    out_path_root = os.path.join(out_dir, 'FerroklastMM.png')
    img.save(out_path_res)
    img.save(out_path_root)
    print(f"FerroklastMM faceplate saved: {out_path_res} ({FK_MM_WIDTH}x{HEIGHT})")


if __name__ == '__main__':
    base_dir = os.path.dirname(__file__)
    out_dir = os.path.join(base_dir, 'assets')
    os.makedirs(out_dir, exist_ok=True)

    fonts = load_fonts(base_dir)
    generate_trigonomicon(fonts, out_dir)
    generate_slidewyrm(fonts, out_dir)
    generate_phasewarpeddrums(fonts, out_dir)
    generate_minimalith(fonts, out_dir)
    generate_amenolith(fonts, out_dir)
    generate_phaseon(fonts, out_dir)
    generate_xenostasis(fonts, out_dir)
    generate_ferroklast_mm(fonts, out_dir)

    print(f"\n✓ All faceplates generated. Resolution: {PX_PER_MM:.4f} px/mm")
