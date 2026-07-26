// The Ogre-Next render backend's expected capability snapshot: one line per
// RenderCaps identity, X(EnumIdentifier, bool). This is the committed mirror of
// the live boot fill in NextBackend.cpp (createRenderSystem) - the register leg
// of render_facade_selfcheck asserts RenderSystem::supports() matches THIS, so a
// boot-fill change without a matching edit here fails the test. It is also the
// next column source for the generated matrix in Docs/render-abstraction.md
// (parsed by Util/update_docs.py).
// Regenerate the matrix after any edit: python3 Util/update_docs.py --write
X(SkyDome, true)
X(DynamicShadows, true)
X(HemisphereAmbient, true)
X(SunExposureLinkage, true)
X(AnimatedNormalMappedWater, true)
X(OffscreenOwnedLayers, true)
X(ProjectedDecals, true)
X(Bloom, true)
X(ScreenSpaceRefraction, true)
X(IblReflections, true)
// the shared output look/grade stage - a native CompositorManager2 grade quad
// after the scene pass; always available on this desktop/mobile-capable flavor
// (Metal/Vulkan render targets carry the off-screen scene texture the grade
// quad samples)
X(OutputGrade, true)
// the native HlmsPbs planar-reflection subsystem (Ogre::PlanarReflections):
// the hand-built reflection workspace generates the mirror's mip chain and
// the planar-on water material compensates the physically-attenuated HlmsPbs
// env term so the mirror reads at classic's strength - probe-verified
// (water_reflection_looks_right runs on BOTH flavors)
X(PlanarReflection, true)
// the editor Scene view's WIREFRAME look: Ogre-Next bakes polygon mode into the
// pipeline state object with no per-target override, so it is the SAME global-
// under-the-invariant road as SceneUnlitView - the 3D-scene datablock set flips to
// line-fill (the 2D/UI set stays solid) only on a Scene-only frame, so it never
// leaks into the other targets (@see RenderWorld::setSceneWireframe)
X(SceneWireframeView, true)
// the editor Scene view's flat lighting-off inspection look: a GLOBAL per-frame
// state (snapshot + hide every light + flat-white ambient, exact restore) armed
// only on a Scene-only frame via the dock-tab visibility rule (@see
// RenderWorld::setLightingSuppressed) - a per-target route is impossible on next
X(SceneUnlitView, true)
