# Upstream PR material for OGRECave/ogre

Three independent patches, prepared locally from the Orkige modernization work
(vcpkg overlay port `ports/ogre`, OGRE 14.5.2). All are formatted with
`git format-patch` against a snapshot of `OGRECave/ogre` master (2026-07-07);
each applies cleanly there on its own (`git am <file>`), and the three also
apply as a series. The numbering only reflects that ordering - they are meant
as three separate PRs. The in-tree copies used by the overlay port target the
v14.5.2 sources; the only difference for the Vulkan surface patch is context
drift from master's later HDR-display additions in `OgreVulkanWindow.cpp`
(unrelated hunks).

Review and submission are done manually by the project owner - nothing here
has been pushed anywhere.

---

## PR 1: `0001-Vulkan-add-VK_EXT_metal_surface-window-support-macOS.patch`

**Title:** Vulkan: add VK_EXT_metal_surface window support (macOS via MoltenVK)

**Body:**

### What

`VulkanWindow::createSurface()` covers Xlib, Win32, Android and Wayland, but
has no Apple branch, so `RenderSystem_Vulkan` cannot create a swapchain on
macOS/iOS even though MoltenVK provides Vulkan there. This PR adds the
missing `VK_EXT_metal_surface` path:

- **`OgreVulkanWindowApple.mm`** (new, ObjC++, ARC): resolves the incoming
  `externalWindowHandle` to a `CAMetalLayer`. It accepts a `CAMetalLayer*`
  directly (any Apple platform) and, on macOS, an `NSWindow*` or `NSView*`
  as well - mirroring what the Metal RenderSystem accepts. If the view is
  not `CAMetalLayer`-backed yet, a `CAMetalLayer` is attached as its hosted
  layer (so it tracks the view size automatically) at point resolution;
  a HiDPI-aware host can pass its own `CAMetalLayer` with a higher
  `contentsScale` instead.
- **`OgreVulkanWindow.cpp`**: creates the `VkSurfaceKHR` from that layer via
  `vkCreateMetalSurfaceEXT` and reports `VK_EXT_METAL_SURFACE_EXTENSION_NAME`
  as the required instance extension. Follows the existing platform branches
  (same `mWindowHandle` handling, same `OGRE_EXCEPT`/`OGRE_VK_CHECK` style).
- **`createSwapchain()`**: honour `VkSurfaceCapabilitiesKHR::currentExtent`
  when the window system defines it. On MoltenVK `min`/`maxImageExtent`
  leave room, so the previous clamp could keep a size the surface no longer
  has - after a window resize every recreated swapchain came back
  `VK_ERROR_OUT_OF_DATE_KHR` and the recreation recursion in
  `acquireNextImage()` overflowed the stack (SIGSEGV). No behaviour change
  on platforms where `min == max == currentExtent`; the clamp path still
  covers `currentExtent == 0xFFFFFFFF` (e.g. Wayland).
- **Portability handling** (needed because MoltenVK is a portability
  implementation, but not Apple-specific):
  - instance: enable `VK_KHR_portability_enumeration` plus
    `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` when the loader
    offers the extension - without it, loaders >= 1.3.216 hide portability
    ICDs and `vkEnumeratePhysicalDevices()` finds no devices;
  - device: enable `VK_KHR_portability_subset` when the physical device
    advertises it, as the Vulkan spec requires.
- **CMake**: `VK_USE_PLATFORM_METAL_EXT` on APPLE, compile the `.mm` with
  `-fobjc-arc`, link QuartzCore (+ AppKit on macOS only).

Guarding is `VK_USE_PLATFORM_METAL_EXT` (+ `TARGET_OS_OSX` inside the `.mm`),
so the path stays iOS-extensible; only the NSWindow/NSView resolution is
macOS-specific.

### Why

MoltenVK translates SPIR-V to MSL, and the Vulkan RS already has full RTSS
support through the glslang plugin - this one missing window branch is the
only structural gap that keeps shader-generated OGRE content from rendering
on macOS (the Metal RS has no RTSS backend). With this patch, an engine can
target one modern render system across Windows/Linux/Android/macOS.

### How tested

macOS 15 / Apple Silicon, MoltenVK 1.4.1 (Homebrew) through the Khronos
loader (`VK_DRIVER_FILES` pointing at MoltenVK's ICD manifest), OGRE built
static via vcpkg. Verified with an SDL3-hosted engine passing the `NSWindow*`
as `externalWindowHandle`:

- instance/device creation, swapchain creation, and a programmatic window
  resize driving the `VK_ERROR_OUT_OF_DATE_KHR` recreate path;
- RTSS-generated shaders (glslang -> SPIR-V -> MSL) for vertex-coloured and
  textured geometry, overlays, render-to-texture picking;
- `writeContentsToFile` screenshots;
- soak: three app suites (demo, scene player, editor self-check) run
  green under ctest with `ORKIGE_RENDERSYSTEM=Vulkan`.

**Reviewer setup note:** on macOS install MoltenVK (`brew install molten-vk`)
and either place its ICD manifest where the loader finds it or run with
`VK_DRIVER_FILES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json` (Intel
brew: `/usr/local/etc/...`). With the LunarG SDK installed, everything works
out of the box. Without a loader, volk already fails gracefully ("Vulkan
unavailable"), same as before this patch.

---

## PR 2: `0002-Vulkan-call-base-RenderSystem-shutdown.patch`

**Title:** Vulkan: call base RenderSystem::shutdown()

**Body:**

### What

`VulkanRenderSystem::shutdown()` is the only render system shutdown that
never calls the base `RenderSystem::shutdown()` (the window-destruction block
it would need instead is `#if 0`'d out). This PR adds the base call right
after the device stall - matching GL3Plus, D3D11 and Metal - and drops the
now-redundant local `_cleanupDepthBuffers()` call plus the dead `#if 0`
block.

### Why

Because the windows are never destroyed during shutdown, `VulkanWindow`
outlives the `VulkanDevice`:

- the window's VMA-backed depth texture is still allocated when
  `~VulkanDevice` runs `vmaDestroyAllocator()`, which trips the VMA debug
  assertion `"Some allocations were not freed before destruction of this
  memory block!"` and aborts every debug build on exit;
- the window is then torn down by the base `~RenderSystem` long after its
  device was deleted (`destroySwapchain()` dereferences the dangling
  `mDevice`).

With the base call, windows release their Vulkan resources while the device
and allocator are still alive.

### How tested

macOS 15 / Apple Silicon, MoltenVK 1.4.1, OGRE 14.5.2 debug build (VMA
asserts active): before the patch every run aborts on exit with the VMA
assertion; after it, clean exit and unchanged rendering. Soaked through
three app suites (demo, scene player, editor self-check) under ctest.
Found while porting an engine to the Vulkan RS, but the bug is
platform-independent - any debug build with an attached window hits the
assert on shutdown.

---

## PR 3: `0003-Metal-export-the-RenderSystem-include-directories-on.patch`

**Title:** Metal: export the RenderSystem include directories on the target

**Body:**

### What

Adds the `target_include_directories(... PUBLIC $<BUILD_INTERFACE:...>
$<INSTALL_INTERFACE:...>)` declaration to `RenderSystem_Metal`, exactly
mirroring what `RenderSystem_GL3Plus`, `RenderSystem_Vulkan` and the other
render systems already do. Both `include/` and `include/Windowing/${OS}` are
exported, matching what the render system compiles against.

### Why

Without it, the installed `OgreTargets.cmake` carries no
`INTERFACE_INCLUDE_DIRECTORIES` for `RenderSystem_Metal`, so a static-build
consumer that links the exported target cannot
`#include <OgreMetalPlugin.h>` to register the plugin via
`Root::installPlugin()` - the header is installed but not on the include
path. Every other render system exports its include directory; Metal is the
odd one out.

### How tested

macOS 15 / Apple Silicon, OGRE 14.5.2 built static through vcpkg with this
patch applied: an external CMake project linking `RenderSystem_Metal` from
the installed `OGREConfig.cmake` compiles `#include <OgreMetalPlugin.h>` and
registers the plugin; without the patch the include fails. (In production use
in the Orkige engine's vcpkg overlay port since the Metal port milestone.)

Applies cleanly to both v14.5.2 and current master (the file is unchanged
between the two).
