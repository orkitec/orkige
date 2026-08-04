# Upstream PR material for OGRECave/ogre-next

Two independent patches to the NULL render system, prepared from the Orkige
deviceless-boot work (`ORKIGE_RENDERSYSTEM=NULL`, see
`engine_render/RenderSystemSelection.h`). Both are formatted with
`git format-patch` against `OGRECave/ogre-next` master (2026-08-04, commit
`2a82de65`); each applies cleanly there on its own (`git am <file>`), and the
two also apply as a series. The numbering only reflects that ordering - they
were submitted as two separate PRs, and neither depends on the other.

**Status: both OPEN** - OGRECave/ogre-next #587 (PR 1, the uninitialised
`Window *`) and #588 (PR 2, the reserved Vao name). A third finding from the
same exercise was raised as issue #589 rather than patched, because it is a
design question - see the bottom of this file.

The engine carries a workaround for each in `engine_render_next/NextBackend.cpp`
(`reserveDevicelessVaoName` and the depth-less window declaration in
`createRenderSystem`). Both become removable once the pin moves past a release
that carries these.

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

## Issue: OGRECave/ogre-next #589

**Title:** NULL render system creates no GpuProgramManager: parsing any
`.material`/`.program` script asserts

**URL:** https://github.com/OGRECave/ogre-next/issues/589

Raised as an issue rather than a PR: the NULL RS creates a HardwareBuffer, Vao and
TextureGpu manager but no `GpuProgramManager`, so `GpuProgramManager::getSingleton()`
asserts (`OgreGpuProgramManager.cpp:52`) when the script compiler translates a
`vertex_program`/`fragment_program` declaration. Three answers are defensible - an
inert `NULLGpuProgramManager`, an honest exception, or "low-level material scripts
are out of scope for this render system" - and picking one is upstream's call, so
the issue describes the symptom and offers to send whichever PR they prefer.

Orkige's deviceless boot sidesteps it by not registering the AtmosphereNpr sky's
`.material`/`.program` media at all when it boots deviceless (`registerAtmosphereMedia`
in `engine_render_next/NextBackend.cpp`) - a run with no GPU has nothing to compile
those into.

---

## How the two patches were verified

`RenderSystem_NULL` + `OgreNextMain` were built static in Debug from the upstream
tree on macOS 15 / Apple Silicon (clang, Ninja, vcpkg-provided dependencies), and a
standalone probe linked against them booted `Root` with `NULLPlugin`, created a
render window, and exercised both defects directly - the Vao names and the
collision after a destroy, and the `"Window"` custom attribute plus
`RenderSystem::getDepthBufferFor` against `Window::getDepthBuffer()`. Baseline
master fails all four checks; each branch fixes exactly its own two; the two
together pass all four. That is a build-and-run verification of the fixes, not a
full Ogre-Next suite run.
