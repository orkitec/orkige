// The classic render backend's expected capability snapshot: one line per
// RenderCaps identity, X(EnumIdentifier, bool). This is the committed mirror of
// the live boot fill in ClassicBackend.cpp (createRenderSystem) - the register
// leg of render_facade_selfcheck asserts RenderSystem::supports() matches THIS,
// so a boot-fill change without a matching edit here fails the test. It is also
// the classic column source for the generated matrix in
// Docs/render-abstraction.md (parsed by Util/update_docs.py).
// Regenerate the matrix after any edit: python3 Util/update_docs.py --write
X(SkyDome, true)
// desktop truth (GL3Plus/Vulkan - what the selfcheck boots); a GLES2/WebGL
// context without depth-texture render targets answers false at runtime
X(DynamicShadows, true)
X(HemisphereAmbient, false)
X(SunExposureLinkage, true)
X(AnimatedNormalMappedWater, false)
X(OffscreenOwnedLayers, false)
// classic has no deferred/projective decal path - the honest subset is a
// surface-aligned textured quad (RenderDecal), which is NOT true projected
// decals, so the capability answers false (@see Docs/render-abstraction.md)
X(ProjectedDecals, false)
// GATED FALSE on classic: the bloom compositor path is implemented (media +
// shaders + the viewport-mask 2D split) but held off until classic OGRE's
// compositor render_scene pass drives the RTSS shader generation the
// shader-only render path needs (it crashes on materials that render on the
// main RTSS viewport - see RenderBackend::bloomSupported + the bloom parity
// debt in Docs/render-abstraction.md). An enabled bloom degrades to no pass.
X(Bloom, false)
X(ScreenSpaceRefraction, false)
// desktop truth (GL3Plus/Vulkan - what the selfcheck boots); a GLES2/WebGL
// context without GLSL ES 3.0 answers false at runtime
X(IblReflections, true)
