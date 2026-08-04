# Sound

Orkige plays three kinds of audio, all through one `SoundManager`
(`orkige_engine/engine_sound/`):

| kind | asset | how it is loaded |
| --- | --- | --- |
| sound effects | `.wav`, `.caf` (Apple platforms) | decoded whole into one voice |
| **procedural sound effects** | **`.sfs`, `.osfx`** | **SYNTHESIZED at load from sound parameters** |
| music | `.ogg` (Vorbis) | streamed through a ring the main thread fills |

Most of this page is about the middle row; [the audio
backend](#the-audio-backend) below covers the tier all three sit on. The mixer
(per-source volume x mixer group x master), positional audio, per-play
pitch/volume variation and the Lua `sound` surface are shared by all three and
documented in [the Lua API reference](lua-api.md).

## The audio backend

`engine_sound/AudioBackend.h` is the ONE door to the output device, the mixing
graph and the voices on it. Handles are opaque and the single-file audio
library behind it is compiled in exactly one translation unit
(`engine_sound/MiniaudioImpl.cpp`), so it never reaches a header, the neutral
umbrella or the precompiled header — the same containment
`engine_sound/StbVorbisImpl.cpp` keeps around the Vorbis decoder.

**The engine decodes its own audio.** `SoundUtil::loadSoundData` is the one
place a sound file becomes samples (`.wav`, `.caf`, `.sfs`/`.osfx`), and
`MusicDecode` is the one place compressed music does; the backend's own file
decoders are compiled out. One decode path, never two.

**Streamed music decodes on the MAIN thread.** A voice is fed from a lock-free
ring: `SoundManager::update` (main thread) decodes into whatever room the ring
has, the mixer's device thread drains it. That is deliberate — the Vorbis
decoder and the resource read behind it are main-thread facts, and the
[filesystem funnel](filesystem.md) is not built to be entered from an audio
callback. The ring carries about two seconds of cushion, so an occasional long
frame costs nothing; a genuine underrun plays silence and the track keeps
going, and a non-looping track ends by itself once its drained ring empties.

**Positional audio** is the inverse-distance model, clamped at both ends: a
source plays at full volume out to a reference distance, falls off as
`ref / (ref + rolloff * (clamp(d, ref, max) - ref))`, and stops falling past a
maximum distance. Only MONO sources are placed in the world; a stereo clip is
a finished mix and plays as authored. Streamed music is never positional.

**Volumes are 0..1 everywhere** — source, mixer group and master — so one
scale covers all three and `effective = base x group`, with the master over
the whole graph.

### Silence is a property of the RUN

**An automated run opens the SILENT device.** Not a test, not a scene, not a
component — the run. `AppHost::initialise` tells the sound system what kind of
run this process is (`SoundManager::setAutomatedRun`, from the same
`automatedRun` flag that decides vsync), and every host comes through it:
editor, player, samples, a native game module, and `--run-tests`. So a scripted
run never reaches the speakers of the machine it runs on, and no test
registration has to remember to ask.

The choice is also **handed down to child processes** through the environment,
so the player an automated editor spawns makes the same one.

The silent device is a real device in every way that matters: it consumes
samples in real time, so playheads advance and rings drain exactly as they
would audibly, and sound-asserting tests pass unchanged. It is preferred over
a zero master volume because it never opens the machine's audio hardware at
all — nothing to steal, no route change, nothing that can interrupt what the
developer is listening to.

**`ORKIGE_AUDIO_BACKEND` overrides the default, either way**, because a
developer sometimes DOES want to hear a scripted run:

| value | effect |
| --- | --- |
| unset | the run decides: silent when automated, the machine's device otherwise |
| `null` / `silent` / `off` | the silent device, even for a human run |
| `auto` (or anything else) | the machine's own device, even for an automated run |

The decision is the pure `SoundManager::resolveSilentDevice`, unit-tested with
no device in sight — it is the kind of rule that regresses without failing
anything, so it has a test of its own.

On iOS the backend sets the AMBIENT audio-session category and activates the
session before it touches the hardware: a game's audio is not the user's
primary media, so it mixes with whatever they are already playing and honours
the ringer switch. A game that wants to play through silence sets its own
category before the sound system starts.

## Why a parameter file

A recorded effect needs an audio tool to make and a binary blob to store. A
PROCEDURAL effect is about a hundred bytes of numbers the engine turns into
samples at load: a few microseconds of synthesis buys an asset that is
diffable, reviewable, hand-tunable, hot-reloadable and writable by an agent
that has only a text channel. The engine never bakes it - the parameters ARE
the asset.

## The parameter model is a standard, not an invention

The parameters, the binary file layout and the archetype generators are the
sfxr model: the de-facto standard for procedural game sound effects, shared
field for field by the free authoring tools of that family. That is the whole
point of adopting it - a designer dials a sound in whichever of those tools
they like, saves the `.sfs`, drops it into `assets/`, and the engine plays what
the tool played.

### Provenance

The model is an external, publicly documented specification. This engine
implements it from that documentation:

* the synthesizer (`orkige_core/core_util/SfxSynth.cpp`), the binary reader
  (`SfxAsset::parseBinary`) and the archetype generators are written here;
* **no code is taken from any implementation of the model.** The forks in that
  family carry different licences (some copyleft), so none of them is a source
  we copy from - only the format and algorithm description are;
* the parameter NAMES in this engine are our own spelling of the standard's
  `p_*` names; the table below is the mapping.

This is the same footing as the other external formats the engine consumes
(glTF meshes, Lottie vector animation, XLIFF string tables, OGG Vorbis music):
the format is named because naming it is what makes interoperability legible.

## The two carriers

`core_util/SfxAsset` reads both into the one `SfxDesc` (`core_util/SfxDesc.h`).
There is one parameter model and two codecs - never two models.

### `.sfs` - the standard binary parameter file

Read-only: the authoring tools own writing it. Versions 100, 101 and 102 are
read. The layout is a little-endian `int` version followed by the values in a
fixed order, written field by field with **no struct padding** (the one-byte
filter flag is followed immediately by the next float):

| # | field | 100 | 101 | 102 |
| --- | --- | --- | --- | --- |
| 1 | version (`int`) | yes | yes | yes |
| 2 | wave type (`int`: 0 square, 1 saw, 2 sine, 3 noise) | yes | yes | yes |
| 3 | `soundVolume` | - | - | yes |
| 4 | `baseFreq` | yes | yes | yes |
| 5 | `freqLimit` | yes | yes | yes |
| 6 | `freqRamp` | yes | yes | yes |
| 7 | `freqDeltaRamp` | - | yes | yes |
| 8 | `duty` | yes | yes | yes |
| 9 | `dutyRamp` | yes | yes | yes |
| 10 | `vibratoStrength` | yes | yes | yes |
| 11 | `vibratoSpeed` | yes | yes | yes |
| 12 | `vibratoDelay` | yes | yes | yes |
| 13 | `attack` | yes | yes | yes |
| 14 | `sustain` | yes | yes | yes |
| 15 | `decay` | yes | yes | yes |
| 16 | `punch` | yes | yes | yes |
| 17 | filter flag (ONE byte) | yes | yes | yes |
| 18 | `lpfResonance` | yes | yes | yes |
| 19 | `lpfFreq` | yes | yes | yes |
| 20 | `lpfRamp` | yes | yes | yes |
| 21 | `hpfFreq` | yes | yes | yes |
| 22 | `hpfRamp` | yes | yes | yes |
| 23 | `phaserOffset` | yes | yes | yes |
| 24 | `phaserRamp` | yes | yes | yes |
| 25 | `repeatSpeed` | yes | yes | yes |
| 26 | `arpSpeed` | - | yes | yes |
| 27 | `arpMod` | - | yes | yes |

Every value but the two ints and the flag is an IEEE-754 single. A version 102
file is 105 bytes. Fields a version does not carry keep their defaults.

Two fields the format carries are deliberately NOT part of it:

* `masterVolume` - the standard's app-level output slider. The authoring tools
  default it to a low monitoring level, so an imported file gets this engine's
  default (1.0) and plays at a usable volume.
* `seed` - see [determinism](#determinism-is-a-contract).

### `.osfx` - the text twin

The same parameters as one directive per line, so an agent can write a sound
with `write_project_file` and a human can diff, review and hand-tune one. `#`
starts a comment. Values are the standard's normalized weights: `0..1`, or
`-1..1` for the signed sweeps (`freqRamp`, `freqDeltaRamp`, `dutyRamp`,
`lpfRamp`, `hpfRamp`, `phaserOffset`, `phaserRamp`, `arpMod`).

```
# a coin pickup, quieter and with a longer tail
version 1
preset coin
soundVolume 0.4
decay 0.3
```

`preset NAME` seeds every parameter from an archetype generator; explicit
directives override individual ones. That precedence is **structural, not
textual**: the parser applies the preset before any other directive no matter
where the `preset` line sits, so a file's meaning never depends on how its
lines happen to be sorted. `seed N` is resolved first of all, because it picks
WHICH member of the archetype's family the file names.

The full directive list is the field table above plus `version`, `preset`,
`wave` (`square`/`saw`/`sine`/`noise`), `filter` (0/1) and `seed`. Every
parameter's meaning is documented field by field in `core_util/SfxDesc.h`.

Malformation is an error, not a shrug: an unknown or duplicated directive, a
wrong arity or a garbage value fails with `line N: ...` and leaves the previous
sound loaded (the same contract `.omat` and `.oui` keep). A value merely out of
RANGE is not the parser's business - the synthesizer clamps it and logs what it
clamped, so a mistyped weight still makes a sound and says so.

## The archetypes

`preset` takes one of the standard generator names. Each is a documented recipe
of ranges - a FAMILY of sounds - so `seed` picks a member and the same seed
always picks the same one:

| preset | also accepted | typical use |
| --- | --- | --- |
| `coin` | `pickup` | a collected pickup |
| `laser` | `shoot` | a shot |
| `explosion` | | an explosion |
| `powerup` | | an upgrade, a level-up |
| `hit` | `hurt` | an impact, taking damage |
| `jump` | | a jump |
| `blip` | `select` | a UI selection |

## Determinism is a contract

The authoring tools draw their noise from a global, unseeded generator - fine
for a tool, useless for an ASSET, which must sound the same on every load and
in every test. `SfxDesc::seed` (this engine's addition, absent from `.sfs`)
seeds both the noise oscillator and the archetype generators through an
explicit integer generator, so one stored effect renders byte-identical samples
every time.

## Playing one

A parameter file IS a sound file to everything above the loader, because the
extension dispatch happens where a wave file is decoded
(`SoundUtil::loadSoundData`). Nothing about playback is special:

```lua
local sound = world.getSound(self.id)
sound:addSound("coin", "coin.osfx", false, true)
sound:setGroup("coin", "sfx")
sound:setPitchVariation("coin", 0.1)
sound:play("coin")
```

Synthesis happens once, at `addSound`; the samples then feed a mixer voice like
any decoded file's, so an interruption rebuild, positional audio and the mixer
all work unchanged. A file that cannot be read or parsed leaves
the source registered but SILENT with one honest error line - one bad asset
costs its own sound and nothing else.

Rendering is mono 16-bit at a fixed 44100 Hz. The rate is not a parameter: the
standard's envelope and period constants are expressed in samples at it, so
rendering at another rate would change the sound.

## In the editor

Selecting a `.sfs` or `.osfx` in the asset browser opens the sound parameter
panel in the Inspector:

* **Audition** renders the current values and plays them immediately, with no
  play session - the tune-and-hear loop. The editor opens an audio device
  lazily on the first audition (an editor that never auditions never touches
  the sound hardware) and says so honestly when there is none.
* **Export WAV** writes the synthesized samples out as a standard 16-bit PCM
  `.wav` beside the asset, for handing to a sound designer, a video editor or
  any other tool. It is an export convenience - the engine keeps playing the
  parameters.
* **Generate** draws a new member of an archetype's family (each pick advances
  the seed, so choosing the same archetype again gives another sound of that
  kind).
* Every parameter is a row labelled with its own directive name, so the panel
  doubles as the grammar reference. **Apply** rewrites the `.osfx` as canonical
  text; a `.sfs` is read-only here (the authoring tools own writing it), and
  **Save as .osfx** writes its parameters out as the editable text twin beside
  it - the same sound, now tunable in the editor and diffable in review.
* **Create > New Sound** in the asset browser writes a `.osfx` carrying an
  archetype's own numbers spelled out as tunable directives.
* A `.osfx` opens in the embedded script editor like any text asset, with live
  line-numbered parse diagnostics from the same parser the runtime uses.

## Where the pieces live

| what | where |
| --- | --- |
| the parameter model + archetype generator table | `orkige_core/core_util/SfxDesc.h` |
| the synthesizer (pure, headless) | `orkige_core/core_util/SfxSynth.{h,cpp}` |
| both codecs (text grammar + binary reader) | `orkige_core/core_util/SfxAsset.{h,cpp}` |
| the RIFF/WAVE encoder | `orkige_core/core_util/WavWriter.{h,cpp}` |
| the loader that makes it a sound file | `orkige_engine/engine_sound/LoadSfxData.cpp` |
| the editor's audition stage | `tools/editor/SfxAuditionStage.{h,cpp}` |

Tests: `SfxSynthTests` / `SfxAssetTests` / `WavWriterTests` (unit, headless),
`player_sfx_selfcheck` (both flavors - text and binary files synthesized,
uploaded and played through the real `SoundManager`, plus the malformed-file
verdict) and the `editor_asset_browser` sound leg (create, audition, export).
