# Upstream PR material for OGRECave/ogre (macOS display query)

One patch to the macOS GL support layer, prepared from the Orkige classic-flavor
work (`ORKIGE_RENDERSYSTEM=GL3Plus`, the default classic render system on
macOS). It is formatted with `git format-patch` against `OGRECave/ogre` master
(2026-08-04, commit `5746dd14`) and applies cleanly there (`git am <file>`); it
also applies to `63c5e68c`, the commit the overlay port `ports/ogre` pins, where
the touched file is byte-identical.

This is a second, separate package for the same repository. The earlier one -
three Vulkan/Metal patches, all merged - is `Docs/upstream/PR_DESCRIPTIONS.md`.
Numbering is package-local: "PR 1" here is unrelated to "PR 1" there.

**Status: PREPARED, NOT FILED.**

The overlay port does not vendor this patch; it is carried here only as the
material to send.

---

## PR 1: `0001-GLSupport-OSX-do-not-dereference-a-NULL-display-mode.patch`

**Title:** GLSupport: OSX - do not dereference a NULL display mode list

**Body:**

`OSXGLSupport::getConfigOptions()` asks macOS for the display modes and uses the
answer without checking it:

```objc
// Video mode possibilities
CFArrayRef displayModes = CGDisplayCopyAllDisplayModes(CGMainDisplayID(), NULL);
CFIndex numModes = CFArrayGetCount(displayModes);
```

`CGDisplayCopyAllDisplayModes` returns NULL for an invalid display, which
includes a process that has no window server session. `CFArrayGetCount(NULL)`
faults inside CoreFoundation.

### Reproduction

A standalone probe built against this tree calls `getConfigOptions()` directly,
the way `GL3PlusRenderSystem::initConfigOptions()` does, with
`CGDisplayCopyAllDisplayModes` interposed to return NULL
(`DYLD_INSERT_LIBRARIES`, `DYLD_INTERPOSE`). macOS 26.5.1 / Apple Silicon,
clang, `OgreMain` + `OgreGLSupport` built static in Debug:

```
CGDisplayCopyAllDisplayModes -> 0x0
calling getConfigOptions()
Segmentation fault: 11
```

Under lldb, at `OgreOSXGL3PlusSupport.mm:83`:

```
* thread #1, stop reason = EXC_BAD_ACCESS (code=1, address=0x0)
  * frame #0: CoreFoundation`__CF_IS_OBJC
    frame #1: CoreFoundation`CFArrayGetCount + 36
    frame #2: probe`Ogre::OSXGLSupport::getConfigOptions() at OgreOSXGL3PlusSupport.mm:83:21
```

### Why it matters

The call is reached from `GL3PlusPlugin::install()`, through
`GL3PlusRenderSystem::GL3PlusRenderSystem()` and
`GLRenderSystemCommon::initConfigOptions()`, so it runs while the render system
is being constructed - before any application code could check a display or
choose a different render system. A GL3Plus process that starts without a window
server session therefore cannot start at all, and dies with a stack that names
CoreFoundation rather than a cause.

macOS puts a process in that state for reasons a headless CI machine does not
have to arrange deliberately: with fast user switching, a shell in a login
session whose console is owned by another user is one.

### The change

Guard the count and the matching `CFRelease`. The mode loop is bounded by
`numModes`, so an empty list falls out naturally: `mVideoModes` stays empty and
the render system reports no video modes, which is the truth on a machine with
no usable display.

### How tested

macOS 26.5.1 / Apple Silicon, clang, static Debug build of `OgreMain` +
`OgreGLSupport` from this tree, the same probe binary in all four runs (only the
library it links differs):

| display list | before | after |
|---|---|---|
| real (normal session) | 2 options, 20 video modes, exit 0 | 2 options, 20 video modes, exit 0 |
| NULL (interposed) | `Segmentation fault: 11`, exit 139 | 2 options, 0 video modes, exit 0 |

The real-display columns are identical, so the patch changes nothing on a
machine with a display; the NULL row is the whole difference.

`CGDisplayCopyAllDisplayModes` has exactly one call site in the tree, so no
other file needs the same guard.

### Provenance

Found while running the OGRE-based flavor of
[orkige](https://github.com/orkitec/orkige) on macOS. That engine boots GL3Plus
through `Root::installPlugin()`, and 24 crash reports over two days on one
machine - across four different executables, `EXC_BAD_ACCESS`,
`KERN_INVALID_ADDRESS at 0x0000000000000000` - carry the identical stack:

```
__CF_IS_OBJC                                    CoreFoundation
CFArrayGetCount                                 CoreFoundation
Ogre::OSXGLSupport::getConfigOptions()          (application)
Ogre::GLRenderSystemCommon::initConfigOptions() (application)
Ogre::GL3PlusRenderSystem::initConfigOptions()  (application)
Ogre::GL3PlusRenderSystem::GL3PlusRenderSystem()
Ogre::GL3PlusPlugin::install()
Ogre::Root::installPlugin(Ogre::Plugin*)
```

On that machine the state is reachable on demand: with fast user switching in
use, a shell in a login session whose console is owned by another user makes
every GL3Plus binary exit 139 immediately after the render system is created.
Display sleep was checked against the power-management log and is not the
trigger.
