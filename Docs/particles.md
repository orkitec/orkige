# Particles

`ParticleComponent` (`engine_gocomponent/ParticleComponent.{h,cpp}`) is the
CPU-simulated, batched particle emitter. The pure, renderer-free, seeded
simulation lives in `engine_gocomponent/ParticleSim.h` (headless-testable,
deterministic — the same seed produces the same sequence). Every live particle
becomes one vertex array submitted through the facade `SpriteBatch` each frame,
so a whole system is a SINGLE draw call, on BOTH render flavors.

## 2D (default)

A planar (XY) emitter: continuous rate and/or bursts, cone spawn direction,
semi-implicit-Euler integration under a Vec2 gravity + damping, per-particle
lifetime, spin, and size/colour interpolated start→end over life (shaped by a
`core_tween/EaseLibrary` curve). Atlas-frame UVs reuse `SpriteComponent`'s
shared primitive; `zOrder` uses the same painter's window as sprites. The batch
hangs off the world root (world-space emission), so particles fly free of the
emitter node.

## 3D + weather

The SAME component grows a reflected `space3D` mode (default OFF, so all 2D
content is byte-identical). When on it runs the 3D world-space path:

- **Emission volumes** — `emissionVolume` = point / sphere (`volumeExtents.x` =
  radius) / axis-aligned box (`volumeExtents` = half-extents), plus a Vec3
  `spawnOffset3D` and a Vec3 `direction3D` cone axis.
- **Dynamics** — Vec3 `gravity3D` + a constant Vec3 `wind` acceleration (weather
  shear), plus an optional sinusoidal `flutterAmplitude`/`flutterFrequency`
  sideways sway (snow drift).
- **World vs local** — `worldSpace` true (the weather default) keeps particles
  in world space so they do NOT follow a moving emitter; false stores them
  emitter-local so they ride it.
- **Rendering** — CPU-billboarded camera-facing quads. The window camera's
  world-space right/up axes come from its view matrix (its first two rows), and
  `ParticleSim::billboardCorners` builds each quad; a non-zero `stretch` uses
  `ParticleSim::streakCorners` to elongate the quad along the particle
  velocity's on-screen direction (rain streaks). Corners are world-space Vec3
  positions fed to the same `SpriteBatch` (it renders in the 3D scene pass with
  the perspective camera), so no new backend object was needed — one draw per
  emitter, textured, alpha or additive.

### Weather presets

Weather is content, not a new system — authored purely through the reflected
tunables:

- **Rain** — a wide thin box volume above the scene, fast fall, a `wind` shear,
  and `stretch` > 0 for velocity-stretched streak billboards.
- **Snow** — small round flakes, slow `gravity3D`, and `flutter*` for the
  sideways sway.

`Util/make_particle_textures.py` (stdlib only, the `orkige_png` codec) generates
the soft `particle_dot.png` (flakes/sparks) and `particle_rain.png` (streak)
textures. `samples/hello_orkige` ships a live rain + snow leg (`demo_particles3d`
selfcheck, both flavors — a hide/show triangle-count probe that also LOGS a
measured per-frame sim + billboard cost).

## Reflected tunables

All the 3D/weather knobs are reflected properties (`OPROPERTY` in
`ParticleComponent.cpp`), so they are the single MCP / inspector / Lua
`self.<name>` / scene-serialization surface (no new MCP verbs, no new Lua
tables): `space3D`, `worldSpace`, `emissionVolume`, `volumeExtents`,
`gravity3D`, `wind`, `direction3D`, `stretch`, `flutterAmplitude`,
`flutterFrequency`, `additive`, `maxParticles`.

## Mobile budget

`maxParticles` is a HARD cap enforced in the sim; the pool is reserved up front
so the steady state is allocation-free (the `TAG_PARTICLES` memory-growth seam
fires zero times per frame). The default cap is a few hundred; the weather demo
runs a couple hundred rain + snow particles each.

## Tests

- `tests/engine/ParticleSimTests.cpp` — the pure headless suite: 2D emission /
  integration / lifetime / caps / determinism, plus the 3D legs (emission-volume
  containment, world-vs-local under a moving emitter, gravity + wind integration,
  lifetime reaping, cap enforcement, allocation-free tick, and the billboard /
  velocity-streak corner math against known camera axes).
- `demo_particles` (2D) and `demo_particles3d` (3D + weather) — the headed
  end-to-end selfchecks, both run per render flavor.
