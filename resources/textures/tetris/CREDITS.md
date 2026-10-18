# Tetris block theme

The seven `block_*.png` files here are crops from a single OpenGameArt sheet.

| | |
|---|---|
| Asset | [Colored Metal tiles 1b](https://opengameart.org/content/colored-metal-tiles-1b) |
| Author | sgtcrispy (also on [Liberated Pixel Cup](https://lpc.opengameart.org/content/colored-metal-tiles-1b)) |
| Licence | **CC0 1.0** - public domain, no attribution required |
| Source file | `Metal_tiles1_extra_4_colors.png`, 1024x768 |

The sheet is eight colour palettes crossed with six metal patterns: diamond
plate, a solid plate, panels, rivets, a grate and slats, each drawn at 32x32
and published at 4x. Every file here is one 128x128 tile cropped straight out
of it, unmodified.

## The crops

Tetris needs seven blocks and the sheet offers 48 tiles, so the choice was
which seven. Each block takes a different colour *and* a different pattern:
the colour is what a player reads at a glance, and the pattern is what still
tells two pieces apart when the lighting flattens the hue.

| Block | Sheet cell (col, row) | Pattern | Palette |
|---|---|---|---|
| `block_i_diamond_plate.png` | 6, 3 | diamond plate | teal |
| `block_o_solid_plate.png` | 7, 0 | solid plate | magenta |
| `block_t_panels.png` | 4, 1 | panels | pink |
| `block_s_rivets.png` | 1, 4 | rivets | green |
| `block_z_grate.png` | 2, 5 | grate | crimson |
| `block_j_gold_slats.png` | 3, 2 | slats | blue with gold |
| `block_l_diamond_plate.png` | 4, 3 | diamond plate | indigo |

Six patterns across seven blocks, so diamond plate is the one repeat: I and L
are the two pieces least likely to be confused for anything, and both read
correctly from their colour.

## Why the colours changed

The sheet has no yellow and no orange palette, so this is not the classic
cyan/yellow/magenta/green/red/blue/orange set. `TetrominoDefinition::color` in
`games/tetris/src/TetrisManager.cpp` was retuned to each tile's own dominant
colour rather than left describing blocks that are no longer painted in it.
That colour drives the emissive term and the preview, so the two have to
agree.

## Re-cropping

The tiles are `128x128` cells of the source sheet at `(col * 128, row * 128)`,
row-major from the top-left:

```python
from PIL import Image
sheet = Image.open("Metal_tiles1_extra_4_colors.png").convert("RGB")
for name, col, row in [
    ("block_i_diamond_plate", 6, 3),
    ("block_o_solid_plate",   7, 0),
    ("block_t_panels",        4, 1),
    ("block_s_rivets",        1, 4),
    ("block_z_grate",         2, 5),
    ("block_j_gold_slats",    3, 2),
    ("block_l_diamond_plate", 4, 3),
]:
    sheet.crop((col * 128, row * 128, col * 128 + 128, row * 128 + 128)).save(name + ".png")
```

## How they are used

`kBlockTheme` in `games/tetris/src/TetrisBoardView.h` indexes these by
`TetrominoType`. The material's albedo is left white on purpose: `pbr.frag`
computes `albedo.rgb * texture(...)`, so a coloured albedo multiplies into the
texture and the two hues muddy. White lets the tile be the only thing deciding
the block's colour.

They only show at all because `resources/models/cube.glb` ships without
`TEXCOORD_0`; `MeshImport::ImportModel` now generates box-projected UVs for
meshes that have none, which is why the tile covers a whole face instead of
sampling texel (0, 0) everywhere.