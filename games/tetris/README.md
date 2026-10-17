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

Moving and soft-dropping are delayed-autorepeat: one step on the press, then a
repeat after 0.15 s, then every 0.05 s.

## What lives where

```
games/tetris/
├── src/
│   ├── TetrisTypes.h      Pure data: pieces, board state, actions
│   ├── TetrisManager.*    The rules. No engine, no window, no key codes
│   ├── TetrisComponents.h The game's ECS components
│   ├── TetrisBoardView.*  Mirrors the rules into a Forge::Scene
│   ├── TetrisInputLayer.* Maps engine key events to game actions
│   └── TetrisApp.*        Window, scene, frame loop
└── tests/                 ForgeTetrisTests
```

The game has no assets of its own: it builds its scene in code and draws the
engine's `resources/models/cube.glb` for every block.

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

## Tests

```bash
cmake --build build-tetris --target ForgeTetrisTests
./build-tetris/bin/ForgeTetrisTests
ctest --test-dir build-tetris -R ForgeTetrisTests
```