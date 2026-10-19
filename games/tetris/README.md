# Forge Tetris

A complete game, built on the Forge engine and separate from it.

```bash
./run-tetris.sh              # build Debug and play
./run-tetris-debug.sh        # build under ASan and play under gdb
```

```
build-tetris/bin/ForgeTetris
```

## Controls

| Key | Action |
|-----|--------|
| Left / A | Move left |
| Right / D | Move right |
| Down / S | Soft drop |
| Up / W / X | Rotate clockwise |
| Z | Rotate counter-clockwise |
| Space | Hard drop |
| P / Escape | Pause |
| R | Restart |
| V | Switch the flat 2D view and the angled 3D well |
| M | Mute and unmute the music |
| N | Next song in the music playlist |
| B | Previous song in the music playlist |

Each song plays to its end and the next one starts, wrapping at the end of the
list. The HUD shows `MUSIC: n/total title`. The music starts with the game - the
menu is silent - and fades out on pause and game over.

Moving and soft-dropping are delayed-autorepeat: one step on the press, then a
repeat after 0.15 s, then every 0.05 s.

A hard drop **packs the stack** on the way in: every block with empty space
under it falls into it, column by column, keeping the order it was dropped in.
That is what lets a hard drop complete a row a hole was keeping open - the
block above the hole falls into it and the row goes out. A soft drop and a
gravity lock leave the stack exactly where it stands; only the hard drop spends
the gaps.

## What lives where

```
games/tetris/
├── src/
│   ├── TetrisTypes.h      Pure data: pieces, board state, actions
│   ├── TetrisManager.*    The rules. No engine, no window, no key codes
│   ├── TetrisComponents.h The game's ECS components
│   ├── TetrisBoardView.*  Mirrors the rules into a Forge::Scene
│   ├── TetrisInputLayer.* Maps engine key events to game actions
│   ├── TetrisMusicTrack.* Pure music: the melody, the synth, the tempo rules
│   ├── TetrisMusic.*      Plays the theme through the engine's AudioEngine
│   └── TetrisApp.*        Window, scene, frame loop
└── tests/                 ForgeTetrisTests
```

The game has no authored assets of its own: it builds its scene in code, draws
the engine's `resources/models/cube.glb` in the block tiles from
`resources/textures/tetris/`, and synthesises its theme into
`resources/audio/tetris_theme.wav` on first run.

The dependency arrows all point one way. `TetrisManager` depends on nothing but
GLM. `TetrisBoardView` and `TetrisInputLayer` depend on the engine. Nothing in
`src/Forge/` depends on any of it.

## Why the rules know nothing about the engine

Input arrives as a `Tetris::Action`, not as a key code, so the whole game is
testable with no window and no graphics device. That is not theoretical: the
`ForgeTetrisTests` target links `ForgeCore` only, and covers line clears, wall
kicks, gravity, autorepeat, scoring and game over as plain arithmetic.

The same split is why the game builds its own scene in code
(`TetrisApp::CreateScene`) instead of loading one. A `.forge` file can only
carry what `SceneSerializer` knows how to write, and the engine has no business
serialising a tetromino. The scene is set up once, by the game that owns it.

## State in the scene

`BoardView` publishes the game into the scene for anything inspecting it - a
tool, an agent, or a future UI:

| Entity | Component | Holds |
|--------|-----------|-------|
| `TetrisGame` | `TetrisGameComponent` | running / paused / game over, playfield placement |
| `GameBoard` | `TetrisBoardComponent` | every cell |
| `CurrentPiece` | `TetrisPieceComponent` | the falling piece |
| `GameDisplay` | `TetrisDisplayComponent` | score, level, lines, combo, next piece |

The manager is the single source of truth; these are a per-frame mirror of it.

## Rendering

One entity per occupied cell, created and destroyed as the board changes. A cell
keeps its entity between frames rather than being rebuilt, and cells the board
no longer holds are destroyed - skipping that leaves a block standing at every
cell a piece has ever visited, so the board fills with the ghost of its own
history and never appears to clear.

### The block theme

Each tetromino is drawn in its own metal tile: `kBlockTheme` in
`TetrisBoardView.h` maps `TetrominoType` to a file in
`resources/textures/tetris/`, and the board, the falling piece and the preview
all read it, so a piece looks the same everywhere it appears.

The tiles are crops of *Colored Metal tiles 1b* by sgtcrispy (CC0, public
domain) - a sheet of eight colour palettes crossed with six metal patterns.
Each piece takes a different colour **and** a different pattern: the colour is
what you read at a glance, the pattern is what still tells two pieces apart
when the lighting flattens their hues. See `resources/textures/tetris/CREDITS.md`
for the licence and how to re-crop them.

Two consequences of using the art as the colour, not as a detail over one:

- **Albedo is white.** `pbr.frag` computes `albedo.rgb * texture(...)`, so a
  coloured albedo multiplies into the tile and the block ends up in a colour
  neither the tile nor the theme chose. The piece identity survives as the
  emissive term, which is additive.
- **`TetrominoDefinition::color` follows the art.** The sheet has no yellow and
  no orange, so the seven colours are the tiles' own dominant colours - the
  emissive glow, the preview and the agent-visible `TetrisPieceComponent::Color`
  all read them, and a colour no block is painted in is a colour that lies.

The tiles only show at all because the engine box-projects a mesh that declares
no `TEXCOORD_0`: `cube.glb` stores POSITION and NORMAL alone, so its UVs used
to be zero everywhere and every block sampled one texel.

## Music

Background music is a playlist, in play order:

| Song | File |
|------|------|
| Block Drop | `resources/audio/tetris_block_drop.mp3` |
| Block Stacker | `resources/audio/tetris_block_stacker.mp3` |
| Falling Blocks | `resources/audio/tetris_falling_blocks.mp3` |
| Korobeiniki | `resources/audio/tetris_theme.wav` |

The mp3s are authored songs in the audio tree. The fourth entry is generated by
the game on first run - about twenty-five seconds of arranged music written
sample by sample, so the playlist always has something to play.

### The generated theme

`TetrisMusicTrack` is a small arranger and synthesiser. It writes the music as
arithmetic on PCM rather than playing a recording, which is what lets a test
check the arrangement with no sound card.

The melody is Korobeiniki, the Russian folk song the Tetris theme comes from -
public domain, transcribed from the folk version in Jack Campin's Nine-Note
Tunebook, not any game's arrangement of it. It is in 2/4, which sets the whole
grid: a bar is four eighths, eight sixteenths.

The arrangement borrows its grammar from the three authored songs, measured
rather than guessed:

| | Block Drop | Block Stacker | Falling Blocks | Generated |
|---|---|---|---|---|
| Tempo | 148 BPM | 125 BPM | 128 BPM | 132 BPM |
| Key | minor | minor | minor | D minor |
| Sub + bass | 42% | 59% | 53% | 39% |
| Mids | 53% | 37% | 45% | 59% |

- **Drums.** A kick with a pitch drop (120 Hz down to 45 Hz), a snare built
  from filtered noise plus two body tones, and closed and open hats. The kick
  sits on both beats with the offbeats accented, the snare on the backbeat -
  the groove all three songs share.
- **Bass.** A saw with the fundamental an octave below it for the sub weight
  those mixes are built on, driving eighths with an octave lift on the last
  eighth of each bar.
- **Pads.** Chord tones doubled a few cents apart and spread across the field;
  the detune is what makes a chord shimmer.
- **Lead.** A square with a fading vibrato, sung an octave above the melody's
  written pitch so its fundamental sits in the mids instead of fighting the
  bass.
- **Side-chain.** The pads and arp dip under every kick, snapped down at the
  hit and eased back up - the pumping the reference mixes have. The lead rides
  on top, undipped.
- **Form.** Twenty-eight bars: intro, verse, chorus, bridge, outro, with the
  melody running through it twice. The bridge drops the drums, the intro opens
  on a ghost kick, and the last bar of the outro drops its weight so the loop
  has somewhere to land.

The loop is seamless by construction: every bus keeps a quarter second of
silence at the end that no voice can sound into, so a sustained pad or an open
hat cannot be cut off at the wrap.

The mp3s in the playlist are what the arrangement is modelled on, and the
arrangement is what the generator contributes; `TetrisMusic` is the engine half
- generate the theme, build the playlist from the songs that are on disk (a
missing file is reported and skipped, never a song that silently never plays),
play on the `Music` bus, and follow the game state. `N` and `B` skip, `M`
mutes, and the tempo rises with the level.

Audio is optional in the engine, so a machine with no output device plays the
game silently rather than failing to start.

## Tests

```bash
cmake --build build-tetris --target ForgeTetrisTests
./build-tetris/bin/ForgeTetrisTests
ctest --test-dir build-tetris -R ForgeTetrisTests
```