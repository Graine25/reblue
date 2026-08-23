#!/usr/bin/env python3
"""Compose the button glyph sheet: the shipped controller block plus a key atlas.

The disc ships d2anime\\res\\cmn_help_menue as a 4x4 grid of 64x64 cells, eleven
of them named by d2anime\\Uv.csv. Those eleven names are what every prompt in the
game reads, and engine/glyph_set.cpp repoints them at runtime.

Pointing them at a second fixed 4x4 block would only ever be right for the
shipped keybind defaults, so instead the sheet carries one cell per bindable
key. A cell is indexed by pad button and a pad button has one bound key, so
eleven cells still suffice: rebinding moves a UV rather than needing new art.

  cell 0..31    the shipped controller block, pasted verbatim in the top-left
  cell 32..     one per name in bd::platform::kBindableKeys, in that order
  next cell     the four arrow keys as a cluster, for the D-pad prompt
  next three    the Shift/Ctrl/Alt caps, for a bind's modifier prefix
  then          eleven cells per pad in PAD_SETS, in the order engine's kCells
                names them, for a player whose controller is not a 360 pad

Keyboard art is Xelu's FREE Controller & Key Prompts, CC0, vendored under
thirdparty/xelu-prompts.

  python3 tools/build_glyph_sheet.py --out res/art/cmn_help_menue.png
"""

import argparse
import pathlib
import re
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("Pillow is required: pip install Pillow")

CELL = 64
COLS = 8
ROWS = 24

# The shipped block keeps its own 4x4 arrangement in the top-left quadrant, so
# it can be pasted without rescaling and its cells stay where Uv.csv says.
SHIPPED_CELL = 4
KEY_CELL_BASE = 32

# Where a glyph's ink sits inside its cell, measured off the shipped sheet. Every
# face button (A, B, LB, RB, LT, RT) inks exactly x 6..44, and the tightest
# footer on the disc, d2anime\L_ftr.csv, sets its label 44 past the icon origin.
# So 38 is the width that clears the label in every slot, and a cap wider than
# it is scaled down rather than allowed to run under the text. The taller
# shipped cells (BACK, START, the D-pad) only ever sit in wider gutters, so
# their extra width is not a footprint we can borrow.
INK_LEFT = 6
INK_BAND_TOP = 12
INK_BAND_HEIGHT = 40
INK_MAX_WIDTH = 38

# Modifier caps draw beside a key cap rather than beside a label, so they get
# the cell's full usable width instead of the gutter rule above. Shift uses
# Xelu's compact 2:1 cap so all three stay legible at the same drawn height.
MODIFIERS = ("Shift_Alt", "Ctrl", "Alt")
MOD_INK_LEFT = 4
MOD_INK_MAX_WIDTH = 56

# rex keybind name -> Xelu cap stem, where the two disagree. Anything absent
# here uses the bind name unchanged.
CAP_ALIASES = {
    "LMB": "Mouse_Left", "RMB": "Mouse_Right", "MMB": "Mouse_Middle",
    "Return": "Enter", "Escape": "Esc", "Backtick": "Tilda",
    "Comma": "Mark_Left", "Period": "Mark_Right",
    "LBracket": "Bracket_Left", "RBracket": "Bracket_Right",
    "PageUp": "Page_Up", "PageDown": "Page_Down", "CapsLock": "Caps_Lock",
    "Delete": "Del",
    "Left": "Arrow_Left", "Right": "Arrow_Right",
    "Up": "Arrow_Up", "Down": "Arrow_Down",
    # The numeric keypad borrows the main row's caps. Xelu draws no keypad set,
    # and a bare digit reads correctly enough on a prompt.
    "NumpadPlus": "Plus", "NumpadMinus": "Minus",
    "NumpadStar": "Asterisk", "NumpadSlash": "Slash",
    **{f"Numpad{n}": str(n) for n in range(10)},
}

ARROWS = ("Arrow_Up", "Arrow_Left", "Arrow_Down", "Arrow_Right")

# The eleven prompts d2anime\Uv.csv names, in the order engine/glyph_set.cpp's
# kCells lists them. A pad set states its art for each.
PAD_CELLS = ("A", "B", "X", "Y", "BACK", "CROSS", "LB", "RB", "LT", "RT",
             "START")

# One block per controller the player can choose, in the order
# engine/glyph_set.cpp's PadSet lists them. The 360 is absent because its art
# is the block the disc already ships, which stays where Uv.csv put it.
#
# The face buttons are mapped by position rather than by the letter printed on
# them: the guest's A is the south button, and a pad that prints something else
# there has to show what the player will actually press. That is why the Switch
# block reads B, A, Y, X against the guest's A, B, X, Y.
PAD_SETS = (
    ("XboxSeries", ("XboxSeriesX_A", "XboxSeriesX_B", "XboxSeriesX_X",
                    "XboxSeriesX_Y", "XboxSeriesX_View", "XboxSeriesX_Dpad",
                    "XboxSeriesX_LB", "XboxSeriesX_RB", "XboxSeriesX_LT",
                    "XboxSeriesX_RT", "XboxSeriesX_Menu")),
    ("PlayStation", ("PS5_Cross", "PS5_Circle", "PS5_Square", "PS5_Triangle",
                     "PS5_Share", "PS5_Dpad", "PS5_L1", "PS5_R1", "PS5_L2",
                     "PS5_R2", "PS5_Options")),
    ("Switch", ("Switch_B", "Switch_A", "Switch_Y", "Switch_X", "Switch_Minus",
                "Switch_Dpad", "Switch_LB", "Switch_RB", "Switch_LT",
                "Switch_RT", "Switch_Plus")),
    ("SteamDeck", ("SteamDeck_A", "SteamDeck_B", "SteamDeck_X", "SteamDeck_Y",
                   "SteamDeck_Inventory", "SteamDeck_Dpad", "SteamDeck_L1",
                   "SteamDeck_R1", "SteamDeck_L2", "SteamDeck_R2",
                   "SteamDeck_Menu")),
)

# Face buttons in guest A, B, X, Y order for every set the cap library carries,
# the 360 first so its block keeps the cells it already had. Appended after the
# arrow cluster so engine/prompt_textures.cpp can compose the pad side of the
# field, script and QTE prompts from the same source it takes key caps from.
PAD_BUTTONS = ("360_A", "360_B", "360_X", "360_Y") + tuple(
    stem for _, art in PAD_SETS for stem in art[:4])

# The cap library engine/prompt_textures.cpp composes the field, script and QTE
# prompts out of at runtime. Cells are twice the footer's and the cap is
# centered with no gutter rule, because each of those destinations states its
# own footprint and the largest, the 256x128 QTE button, should be reached by
# downscaling rather than by blowing a 64px cell up.
CAP_CELL = 128
CAP_MARGIN = 8


def bindable_keys(header):
    """The names in bd::platform::kBindableKeys, in declaration order."""
    text = header.read_text(encoding="utf-8")
    match = re.search(r"kBindableKeys\[\]\s*=\s*\{(.*?)\};", text, re.S)
    if not match:
        sys.exit(f"kBindableKeys not found in {header}")
    return re.findall(r'"([^"]+)"', match.group(1))


def cap_path(src, name):
    stem = CAP_ALIASES.get(name, name)
    path = src / f"{stem}_Key_Light.png"
    return path if path.exists() else None


def trimmed(path):
    image = Image.open(path).convert("RGBA")
    box = image.getchannel("A").getbbox()
    if box is None:
        raise ValueError(f"{path} is fully transparent")
    return image.crop(box)


def fit(image, max_w, max_h):
    scale = min(max_w / image.width, max_h / image.height)
    size = (max(1, round(image.width * scale)), max(1, round(image.height * scale)))
    return image.resize(size, Image.LANCZOS)


def cell_image(ink, left=INK_LEFT, max_width=INK_MAX_WIDTH):
    """One cell with its ink on the shipped sheet's baseline."""
    cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    art = fit(ink, max_width, INK_BAND_HEIGHT)
    cell.alpha_composite(
        art, (left, INK_BAND_TOP + (INK_BAND_HEIGHT - art.height) // 2))
    return cell


def cap_cell_image(ink):
    """One library cell: the cap centered, as large as the margin allows."""
    cell = Image.new("RGBA", (CAP_CELL, CAP_CELL), (0, 0, 0, 0))
    room = CAP_CELL - 2 * CAP_MARGIN
    art = fit(ink, room, room)
    cell.alpha_composite(art, ((CAP_CELL - art.width) // 2,
                               (CAP_CELL - art.height) // 2))
    return cell


def arrow_cluster(src, key):
    """The four arrow keys in the inverted T a keyboard actually uses.

    A plus arrangement would mirror the shipped D-pad more closely, but it needs
    three rows in the same band, which leaves each key too small to read at the
    64px the footers draw. The inverted T needs two.
    """
    out = Image.new("RGBA", (key * 3, key * 2), (0, 0, 0, 0))
    offsets = {"Arrow_Up": (key, 0), "Arrow_Left": (0, key),
               "Arrow_Down": (key, key), "Arrow_Right": (2 * key, key)}
    for stem in ARROWS:
        art = fit(trimmed(src / f"{stem}_Key_Light.png"), key, key)
        ox, oy = offsets[stem]
        out.alpha_composite(art, (ox + (key - art.width) // 2,
                                  oy + (key - art.height) // 2))
    return out


def paste_cell(sheet, index, image, cell=CELL, cols=COLS):
    if index >= cols * ROWS:
        sys.exit(f"cell {index} is past the {cols}x{ROWS} sheet")
    sheet.paste(image, ((index % cols) * cell, (index // cols) * cell))


def build_caps(prompts, keys):
    """The runtime cap library: keys, the arrow cluster, then the pad buttons.

    Indexed from zero rather than from the footer sheet's 32, because nothing
    here carries the shipped controller block.
    """
    cells = len(keys) + 1 + len(PAD_BUTTONS)
    rows = (cells + COLS - 1) // COLS
    lib = Image.new("RGBA", (COLS * CAP_CELL, rows * CAP_CELL), (0, 0, 0, 0))
    for i, name in enumerate(keys):
        path = cap_path(prompts, name)
        if path is not None:
            paste_cell(lib, i, cap_cell_image(trimmed(path)), CAP_CELL)
    cluster = arrow_cluster(prompts, (CAP_CELL - 2 * CAP_MARGIN) // 2)
    paste_cell(lib, len(keys), cap_cell_image(cluster), CAP_CELL)
    for j, stem in enumerate(PAD_BUTTONS):
        path = prompts / f"{stem}.png"
        if not path.exists():
            sys.exit(f"pad button art missing: {path}")
        paste_cell(lib, len(keys) + 1 + j, cap_cell_image(trimmed(path)),
                   CAP_CELL)
    return lib


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--shipped", type=pathlib.Path,
        default=pathlib.Path("W:/reblue/bd-extract/d2anime/res/cmn_help_menue.png"),
        help="decoded shipped sheet, pasted verbatim into the top-left quadrant")
    parser.add_argument("--prompts", type=pathlib.Path,
                        default=root / "thirdparty" / "xelu-prompts")
    parser.add_argument("--header", type=pathlib.Path,
                        default=root / "src" / "platform" / "key_capture.h")
    parser.add_argument("--out", type=pathlib.Path, required=True)
    parser.add_argument("--caps-out", type=pathlib.Path,
                        help="runtime cap library, one 128px cell per key")
    args = parser.parse_args()

    keys = bindable_keys(args.header)
    sheet = Image.new("RGBA", (COLS * CELL, ROWS * CELL), (0, 0, 0, 0))

    shipped = Image.open(args.shipped).convert("RGBA")
    want = SHIPPED_CELL * CELL
    if shipped.size != (want, want):
        sys.exit(f"expected a {want}x{want} shipped sheet, got {shipped.size}")
    sheet.paste(shipped, (0, 0))

    missing = []
    for i, name in enumerate(keys):
        path = cap_path(args.prompts, name)
        if path is None:
            missing.append(name)
            continue
        paste_cell(sheet, KEY_CELL_BASE + i, cell_image(trimmed(path)))
    paste_cell(sheet, KEY_CELL_BASE + len(keys),
               cell_image(arrow_cluster(args.prompts, INK_BAND_HEIGHT // 2)))
    for j, stem in enumerate(MODIFIERS):
        paste_cell(sheet, KEY_CELL_BASE + len(keys) + 1 + j,
                   cell_image(trimmed(args.prompts / f"{stem}_Key_Light.png"),
                              MOD_INK_LEFT, MOD_INK_MAX_WIDTH))

    pad_base = KEY_CELL_BASE + len(keys) + 1 + len(MODIFIERS)
    for s, (name, art) in enumerate(PAD_SETS):
        if len(art) != len(PAD_CELLS):
            sys.exit(f"{name} names {len(art)} cells, expected {len(PAD_CELLS)}")
        for j, stem in enumerate(art):
            path = args.prompts / f"{stem}.png"
            if not path.exists():
                sys.exit(f"pad art missing: {path}")
            paste_cell(sheet, pad_base + s * len(PAD_CELLS) + j,
                       cell_image(trimmed(path)))

    args.out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(args.out)
    print(f"wrote {args.out} {sheet.size}: {len(keys)} keys "
          f"at cells {KEY_CELL_BASE}..{KEY_CELL_BASE + len(keys)}, "
          f"{len(PAD_SETS)} pad sets at {pad_base}.."
          f"{pad_base + len(PAD_SETS) * len(PAD_CELLS) - 1}")
    if missing:
        print(f"no cap art, cells left empty: {', '.join(missing)}")

    if args.caps_out:
        lib = build_caps(args.prompts, keys)
        args.caps_out.parent.mkdir(parents=True, exist_ok=True)
        lib.save(args.caps_out)
        print(f"wrote {args.caps_out} {lib.size}: keys 0..{len(keys) - 1}, "
              f"cluster {len(keys)}, pad {len(keys) + 1}.."
              f"{len(keys) + len(PAD_BUTTONS)}")


if __name__ == "__main__":
    main()
