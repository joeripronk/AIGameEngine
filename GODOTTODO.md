# GODOTTODO

Features to take from Godot 4.8 (`/home/joeri/src/godot`, tag 4.8.0) and add to
the Forge engine, in the order they matter.

The list is not "everything Godot has". It is what this engine is *missing* in
the areas it already works in: a 3D engine with an ECS, a PBR renderer, a
physics solver, Lua scripting, an AI-agent-driven editor and one game built on
top of it. The gaps below are the ones a game hits within its first hour.

Each item says what Godot has, what Forge does instead, and what "done" means.
Every item is finished with unit tests (the pure parts) and, where the defect
is only visible on screen, with an integration check.

## Legend

- [ ] not started
- [~] in progress
- [x] done (with the commit that finished it)

---

## P0 - the things every game needs

### 1. [x] An input action map (`Forge/Core/InputMap.h`)

**Godot has** `InputMap`: named actions ("move_left", "jump", "fire"), each
bound to keys, mouse buttons and joystick axes, with modifier matching, a
deadzone, and a saved remapping the player owns.

**Forge has** raw key codes. Gameplay asks `Input::IsKeyPressed(Key::W)`, so
the binding is written into the code, a remap is impossible, and the same
question is answered differently in C++, in Lua and in the game's input layer.

**Done when**
- An action is a name bound to keys and mouse buttons, with optional
  modifier requirements (Godot's rule: the modifiers a binding asks for
  must be among the ones held).
- The state is asked for by name: pressed / released / just pressed / just
  released / strength, plus `GetAxis("move_left", "move_right")`.
- The state comes from the events, so it is exact ("just pressed" is a
  transition, not a comparison with last frame) and it is testable with no
  window at all.
- Bindings survive a restart: the map loads from and saves to a text file.
- The bindings reach Lua and the game's own input layer.

### 2. [x] Undo/redo in the editor (`Forge/Editor/UndoStack.h`)

**Godot has** a full undo/redo stack in the editor: every scene change is a
command, and Ctrl+Z walks backwards through them.

**Forge has** none. Every property edit an agent or an author makes is
destructive, and there is no way back to the state before the mistake.

**Done when**
- A stack of commands with `Undo`/`Redo`, a redo stack that is cleared by a
  new command, and a merge policy so a drag is one command, not a thousand.
- The editor context's destructive operations (create, destroy, duplicate,
  transform edits, component add/remove, property changes) go through it.
- The editor can undo and redo, and the agent API can too.

---

## P1 - the things a game needs once it has a world

### 3. [x] A keyframe animation system (`Forge/Scene/AnimationComponent.h`)

**Godot has** `AnimationPlayer`, `AnimationLibrary`, `AnimationTree`: tracks
of keyframed properties over time, played back, looped, blended and driven by
a state machine.

**Forge has** none. The one animated thing in the repository (the Tetris
landing shake) is hand-written maths in the game, and a game of any size
needs to move things over time.

**Done when**
- An animation is a set of tracks: a property path, a list of keyframes
  (time, value), and an interpolation mode.
- The property paths address what exists: the transform, and the fields of
  the components.
- A component plays an animation: it advances with the timestep, loops,
  and writes the animated values onto the entity.
- The evaluation maths is pure and unit-tested (interpolation, easing,
  sampling before the first and after the last keyframe).

### 4. [x] Tweening and easing (`Forge/Core/Tween.h`)

**Godot has** `Tween` and `create_tween()`: a value interpolated towards a
target over a duration, with an easing curve, delay, looping and chaining.

**Forge has** easing curves only inside the game
(`games/tetris/src/TetrisAnimation.h`), where they are private to one game.

**Done when**
- The engine owns the easing curves (the same set Godot has) and a tween
  service that runs them: start, update, cancel.
- The curves and the interpolation are pure and unit-tested.

### 5. [x] A character controller and a raycast shape (`Forge/Physics/`)

**Godot has** `CharacterBody3D` (a capsule that walks, slides, climbs steps
and sticks to slopes) and `RayCast3D` / `ShapeCast3D` for line of sight.

**Forge has** an AABB and a sphere, and a raycast that answers a question
about the world's static geometry. Nothing walks.

**Done when**
- A kinematic body moves with a velocity, slides along what it hits, climbs
  steps and does not climb slopes that are too steep - the movement maths is
  pure and unit-tested.
- A raycast answers "what is in front of me", with a hit distance
  (`Physics::RaycastDistance`).

---

## P2 - the things a bigger game needs

### 6. [x] Navigation and pathfinding (`Forge/Navigation/AStar.h`)

**Godot has** `NavigationServer3D`, `NavigationMesh` and `AStarGrid2D/3D`:
agents find their way around a world, and the editor bakes a navigation mesh.

**Forge has** none. The editor is aimed at AI agents, and an agent that
cannot path anywhere is an agent that walks into walls.

**Done when**
- A* over a grid, with a diagonal option and a corner rule, and the path
  smoothed so it does not zigzag.
- The search is pure and unit-tested (the shortest path, the unreachable
  case, the blocked cell).

### 7. [x] Localization (`Forge/Localization/`)

**Godot has** `TranslationServer`, `Translation` resources and a CSV importer:
the game's text is looked up by key, and the player's language chooses the
table.

**Forge has** none. Every string in the game is written into the code.

**Done when**
- A translation table (key -> text) loaded from a text file, a current
  language, and `Translate("key")` that falls back to the key itself.
- The engine and the Lua API both reach it.

---

## P3 - the things that are nice when there is time

### 8. [x] 3D labels and text in the world (`Label3D`)

**Godot has** `Label3D`: a text mesh in the world, with a font, an outline
and a billboard mode.

**Forge has** none - the only text is the ImGui overlay, which cannot be
placed in a scene.

**Done when**
- A font is rasterised into a mesh and a component places it in the world.

**How it is done here.** The engine's font is a built-in vector font
(stb_easy_font, public domain), so the glyphs are quads: the text needs no
texture and no blending, and a label is drawn by the same path as any
other mesh. The label's colour is the entity's material, and the label
rebuilds its mesh only when the text changes.

**What a real font would change.** A .ttf (stb_truetype) would give a
proper typeface, and its glyphs are textured: the alpha is a texture
channel, so the engine's one opaque material path would need either a
second pipeline (blended) or an alpha test. That is a renderer change and
not a text feature, and the built-in font is what the engine can do
without one.

### 9. [x] Instanced rendering (`MultiMesh`)

**Godot has** `MultiMeshInstance3D`: one mesh, many transforms, one draw call.

**Forge has** one draw call per entity. A forest is a thousand draw calls.

**Done when**
- The renderer draws a mesh with many transforms in one call, and the
  statistics report it as one draw.

### 10. [x] Level of detail

**Godot has** `LODMesh` and `OccluderInstance3D`.

**Forge has** frustum culling only: everything inside the frustum is drawn,
however far away it is.

**Done when**
- A mesh switches to a lower-detail version with distance, and the choice
  is unit-tested. (Occlusion culling is not in this list: it is a
  different question - what is *hidden* - and the renderer has no depth
  prepass to build one from.)

---

## Round two - what a game hits after the first hour

The first round was what a game needs to exist at all. This round is what it
needs once it is a game: the things a second player, a second kind of
collision or a second look at the world requires.

### 1. [x] Gamepads in the input map

**Godot has** `InputMap` with joypad buttons and axes, a deadzone and a
direction per axis.

**Forge has** keys and mouse buttons.

**Done when**
- An action can be bound to a pad's button or to one of its axes, the axis
  says which way it counts, and a stick that rests off centre is not a
  player pressing something. The pads are read by the window layer and what
  changed becomes an event, so the map is fed the way it is fed by the
  keyboard.

### 2. [ ] Collision layers and masks

**Godot has** `collision_layer` and `collision_mask` on every body: a body is
on a layer, and it collides with the layers its mask names.

**Forge has** every body colliding with every body.

**Done when**
- A body says which layers it is on and which layers it collides with, and
  a pair that does not agree does not collide. The rule is a pure function
  of the two masks.

### 3. [ ] A curve for the animation's interpolation

**Godot has** `Curve` and `Gradient`: a value that is interpolated along a
shape the author draws.

**Forge has** linear and smooth interpolation, and nothing else.

**Done when**
- A curve is a list of points, and the animation's tracks can use one
  instead of the two built-in shapes.

---

## Notes on what was deliberately not taken

- **A 2D engine** (`CanvasItem`, sprites, tilemaps): a whole renderer, a
  whole scene graph, and a shader pipeline. Forge is a 3D engine; a 2D
  engine in it would be a second engine, not a feature.
- **GDScript**: Forge has Lua, which does the same job, and a second
  scripting language is a second runtime to maintain.
- **The node/scene tree**: Forge has an ECS. Godot's scene tree is a
  different (and, for an agent-driven editor, a worse) way of holding the
  same data.
- **Multiplayer**: a large feature with no use in the engine's own game, and
  one that would need the whole threading model behind it.
