# Upstream PR material for OGRECave/ogre-next

Three independent patches to the NULL render system, prepared from the Orkige
deviceless-boot work (`ORKIGE_RENDERSYSTEM=NULL`, see
`engine_render/RenderSystemSelection.h`). All three are formatted with
`git format-patch` against `OGRECave/ogre-next` master (2026-08-04, commit
`2a82de65`); each applies cleanly there on its own (`git am <file>`), and they
also apply as a series. The numbering only reflects that ordering - they are
separate PRs, and none depends on another.

**Status:** OGRECave/ogre-next #587 (PR 1, the uninitialised `Window *`) and
#588 (PR 2, the reserved Vao name) are MERGED. PR 3 is filed as #590,
answering issue #589.

The engine carries a workaround for each in `engine_render_next/NextBackend.cpp`
(`reserveDevicelessVaoName` and the depth-less window declaration in
`createRenderSystem`), and skips the AtmosphereNpr `.material`/`.program` media
when it boots deviceless (`registerAtmosphereMedia`). All three become
removable once the pin moves past a release that carries these.

---

## PR 1: `0001-NULL-Answer-the-Window-custom-attribute-on-the-windo.patch`

**Title:** NULL: fix uninitialised Window pointer read in getDepthBufferFor

**URL:** https://github.com/OGRECave/ogre-next/pull/587

**Body:**

`RenderSystem::getDepthBufferFor()` reads an uninitialised `Window *` for every
render-window pass under the NULL render system:

```cpp
if( colourTexture->isRenderWindowSpecific() )
{
    Window *window;                                       // uninitialised
    colourTexture->getCustomAttribute( "Window", &window );
    return window->getDepthBuffer();
}
```

`NULLWindow::_initialize` creates its colour texture with
`TextureFlags::RenderWindowSpecific`, so the branch is taken - but no NULL
texture class overrides `getCustomAttribute`, and `TextureGpu`'s base
implementation is an empty no-op:

```cpp
virtual void getCustomAttribute( IdString name, void *pData ) {}
```

The call therefore writes nothing, `window` keeps whatever the stack slot held,
and the next line dereferences it.

### Why it matters

This is not a cosmetic gap. Whether it crashes depends entirely on what happened
to be on the stack: a Debug build can wander through and hand back a garbage
`TextureGpu *` that a compositor pass then uses, and a Release build of the same
code segfaults (`EXC_BAD_ACCESS`). Anyone using the NULL RS for a headless/CI run
hits it at the first pass targeting the window, in a build configuration where
they cannot see why.

Instrumented against master (the probe starts the local at a known poison value
instead of relying on stack luck, and is otherwise a verbatim copy of what
`getDepthBufferFor` does):

```
colour texture isRenderWindowSpecific = 1
getCustomAttribute("Window") -> 0xdeadbeef (window is 0x106640730)
getDepthBufferFor -> 0xd29f6f2a9a890149 (window->getDepthBuffer() is 0x10664bb90)
```

The returned "depth buffer" is a value read out of unrelated memory.

### The change

Add `NULLTextureGpuWindow`, whose only job is to answer that attribute - the NULL
twin of `MetalTextureGpuWindow`, `VulkanTextureGpuWindow`, `D3D11TextureGpuWindow`
and `GL3PlusTextureGpuWindow`, which all exist for exactly this - and pass the
window to `createTextureGpuWindow()` the way the other render systems already do.

Nothing else needed changing: `NULLWindow::_initialize` has been creating a real
`mDepthBuffer` all along, so once the attribute is answered the lookup returns the
window's own depth buffer.

### Verification

macOS 15 / Apple Silicon, clang, `RenderSystem_NULL` + `OgreNextMain` built static
in Debug (the same probe as above, now checking the results instead of just
printing them):

- before - `getCustomAttribute` leaves the caller's pointer untouched,
  `getDepthBufferFor` returns a value read from an unrelated address;
- after - `getCustomAttribute("Window")` returns the window, and
  `getDepthBufferFor` returns exactly `window->getDepthBuffer()`.

Found while making the Ogre-Next backend of
[orkige](https://github.com/orkitec/orkige) run window-less and GPU-less for CI,
which is the first thing that asked the NULL RS to service a render-window pass.
There the Debug build happened to survive on a dereferenceable stack slot and the
Release build of the same scene segfaulted at `0x28` - which is how this was
tracked down.

---

## PR 2: `0002-NULL-Never-name-a-Vao-0-RenderQueue-s-reserved-nothi.patch`

**Title:** NULL: never name a Vao 0 (RenderQueue's reserved value)

**URL:** https://github.com/OGRECave/ogre-next/pull/588

**Body:**

`NULLVaoManager::createVertexArrayObjectImpl` names each new Vao after the size
of the base class's live-Vao set:

```cpp
uint32 idx = (uint32)mVertexArrayObjects.size();
```

Two things follow from that.

**The first Vao is named 0**, which is `RenderQueue`'s reserved "no Vao bound yet"
value, so the first draw call trips

```cpp
OGRE_ASSERT_MEDIUM( vao->getVaoName() != 0u &&
                    "Invalid Vao name! This can happen if a BT_IMMUTABLE buffer was "
                    "recently created and VaoManager::_beginFrame() wasn't called" );
```

in `RenderQueue::renderGL3`/`renderES2` and aborts every Debug build - with a
message that points at an unrelated cause.

**Names are reused.** `mVertexArrayObjects` holds the *live* Vaos, so destroying
one and creating another hands the new one a name a still-live Vao already holds.
`RenderQueue`'s `lastVaoName` check then skips the `CbVao` command between two
different Vaos, which is exactly the batching decision a NULL-RS run is usually
there to measure.

Both are visible on master:

```
vao[0] name = 0
vao[1] name = 1
vao[2] name = 2
vao after a destroy = 2 (still-live vao[2] = 2)
```

### The change

Use a monotonic counter starting at 1, which is what `MetalVaoManager` and
`VulkanVaoManager` already do (`mVaoNames( 1 )` in both constructors).

### Verification

macOS 15 / Apple Silicon, clang, `RenderSystem_NULL` + `OgreNextMain` built static
in Debug:

```
vao[0] name = 1
vao[1] name = 2
vao[2] name = 3
vao after a destroy = 4 (still-live vao[2] = 3)
```

No Vao is named 0, and no two live Vaos share a name.

Found while making the Ogre-Next backend of
[orkige](https://github.com/orkitec/orkige) run window-less and GPU-less for CI:
the Debug run aborted on the assert above at the first draw call (SIGTRAP, exit
133). The workaround there was to park one Vao alive from boot so every real one
landed at 1 or above - which is the shape of this fix, done properly.

---

## PR 3: `0003-NULL-Create-a-GpuProgramManager-so-scripts-can-be-pa.patch`

**Title:** NULL: create a GpuProgramManager, so material scripts can be parsed

**Answers:** https://github.com/OGRECave/ogre-next/issues/589

**Status: OPEN** - filed as https://github.com/OGRECave/ogre-next/pull/590
(branch `null-gpu-program-manager` on the `orkitec/ogre-next` fork).

**Body:**

Fixes #589. Under `RenderSystem_NULL` there is no `GpuProgramManager`, so parsing
any `.material` or `.program` script aborts:

```
Assertion failed: (msSingleton), function getSingleton,
  file OgreGpuProgramManager.cpp, line 52
```

`NULLRenderSystem::_createRenderWindow` creates the HardwareBuffer, Vao and
TextureGpu managers, and nothing creates a `GpuProgramManager`, so `msSingleton`
stays null for the whole process lifetime.

This is option 1 from the issue, held to the condition you set there: worth having
only if it stays small in RAM and in maintenance.

### The change

`NULLGpuProgramManager` is created beside the other managers in
`_createRenderWindow` and destroyed in `shutdown()`. Metal and Vulkan create
theirs in `initialiseFromRenderSystemCapabilities`; NULL's implementation of that
hook is empty and nothing calls it, so the managers' own creation site is the
equivalent place.

It has no members. Both `createImpl` overloads return a `NULLGpuProgram` with no
branch in between, because type and syntax code only matter to a compiler and
there is none. `NULLGpuProgram` is the low level counterpart of the `NullProgram`
OgreMain already carries for high level programs in an unregistered language, and
it is the same handful of lines:

```cpp
void   loadFromSource() override {}
void   unloadImpl() override {}
bool   isSupported() const override { return false; }
size_t calculateSize() const override { return 0; }
bool   setParameter( const String &, const String & ) override { return true; }
```

That is the entire implementation. The two new files are 62 and 97 lines
including the license header; wiring them into the render system is 7 lines.

### Why it stays small

- **No state.** No program factory map, no microcode, no device handle, no
  syntax registry. `D3D11GpuProgramManager` has exactly this shape today
  (`D3D11UnsupportedGpuProgram`, "D3D11 doesn't support assembly shaders").
- **Nothing to keep in sync.** It holds no opinion about shader stages, root
  layouts, syntax codes or capabilities, so a change anywhere else cannot make it
  stale or wrong. There is no code path in it that a future feature has to teach.
- **RAM.** One small `GpuProgram` per declared program, and nothing else. NULL
  advertises no shader profiles, so `isSyntaxSupported()` is false, the compiler
  takes the road it already takes for a program meant for another render system,
  and the program is never loaded: its source is not read from disk, and
  `calculateSize()` reports 0.

### Nothing starts pretending to work

Each declaration is reported as unsupported by the render system, the same way a
D3D11 program is reported under GL:

```
Compiler error: object unsupported by render system in probe.program(1): , Shader name: ProbeVertexProgram
```

and a material built on such programs is honest about what it became:

```
WARNING: material ProbeMaterial has no supportable Techniques and will be blank. Explanation:
Pass 0: Vertex program ProbeVertexProgram cannot be used - not supported.
```

`isSupported()` is overridden rather than left to the syntax lookup, so that
answer does not depend on what the NULL capabilities happen to advertise later.

If you would rather have the honest refusal (option 2 in the issue) instead, say
so and I will send that one; which of the two to carry is your call.

### Verification

macOS 15 / Apple Silicon, clang, Ninja. `RenderSystem_NULL` and `OgreNextMain`
built static in Debug from this branch, and a standalone probe linked against them
booted `Root` with `NULLPlugin`, created a render window through
`Root::createRenderWindow`, added a resource location holding one `.program` (two
`asm` declarations) and one `.material` referencing both, and called
`initialiseAllResourceGroups`.

Baseline master, same probe, same media:

```
GpuProgramManager::getSingletonPtr() = 0x0
Parsing script probe.program
Assertion failed: (msSingleton), function getSingleton, file OgreGpuProgramManager.cpp, line 52.
```

SIGABRT, exit 134. With this branch:

```
GpuProgramManager::getSingletonPtr() = 0x101a95ff0
Compiler error: object unsupported by render system in probe.program(1): , Shader name: ProbeVertexProgram
Compiler error: object unsupported by render system in probe.program(7): , Shader name: ProbeFragmentProgram
scripts parsed
ProbeVertexProgram = 0x101a9d820
  isSupported = 0, calculateSize = 0, hasCompileError = 0
ProbeMaterial = 0x101a9cf60
  supported techniques = 0
PROBE PASSED (0)
```

Exit 0, and the shutdown is clean.

Not covered: the Ogre-Next sample and test suites were not run, and this was built
for macOS with clang only. Nothing outside `RenderSystems/NULL` is touched.

Encountered while making the Ogre-Next backend of
[orkige](https://github.com/orkitec/orkige) run window-less and GPU-less for CI;
the media that triggered it was the AtmosphereNpr sky's own `.material`/`.program`
pair, which the engine now skips registering when it boots deviceless.

---

## How the three patches were verified

`RenderSystem_NULL` + `OgreNextMain` were built static in Debug from the upstream
tree on macOS 15 / Apple Silicon (clang, Ninja, vcpkg-provided dependencies), and
standalone probes linked against them booted `Root` with `NULLPlugin` and
exercised each defect directly:

- PR 1 and PR 2, one probe: the Vao names and the collision after a destroy, and
  the `"Window"` custom attribute plus `RenderSystem::getDepthBufferFor` against
  `Window::getDepthBuffer()`. Baseline master fails all four checks; each branch
  fixes exactly its own two; the two together pass all four.
- PR 3, a second probe: the script parse described in its section above, run
  against baseline master and against the branch, with the libraries rebuilt in
  between so both sides are real runs rather than reasoning.

That is a build-and-run verification of each fix, not a full Ogre-Next suite run.

Two local notes that belong to this machine rather than to the patches:

- Building `OgreNextMain` from a checkout where FreeImage is not installed fails
  on `#include <FreeImage.h>`, because `OgreMain/CMakeLists.txt` adds
  `src/OgreFreeImageCodec2.cpp` to `BROKEN_FILES_IN_UNITY_BUILD` unconditionally
  and `ogre_add_library` compiles that list even though
  `OGRE_CONFIG_ENABLE_FREEIMAGE` resolved to OFF. The verification build worked
  around it locally. It is unrelated to these patches and has not been reported.
- The probes and the upstream build tree live outside this repository; only the
  patches and this file are kept.
