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
// the native HlmsPbs planar-reflection subsystem (Ogre::PlanarReflections):
// a camera reflected across the water plane renders the scene into a reflection
// RTT that the HlmsPbs water datablock samples in its reflection slot (the
// desktop-capable Metal/Vulkan render targets carry the second scene render).
// A GLES/WebGL context would runtime-gate it, but the next flavor's mobile
// targets are Metal/Vulkan, so the bit is unconditional here (default water is
// byte-stable - the reflection subsystem stands up only when a scene opts in)
X(PlanarReflection, true)
