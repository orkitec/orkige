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
// desktop truth (GL3Plus/Vulkan - what the selfcheck boots); a GLES2/WebGL
// context answers false at runtime (gated pending an on-device/browser proof
// run of the compositor chain - see RenderBackend::bloomSupported)
X(Bloom, true)
X(ScreenSpaceRefraction, false)
// desktop truth (GL3Plus/Vulkan - what the selfcheck boots); a GLES2/WebGL
// context without GLSL ES 3.0 answers false at runtime
X(IblReflections, true)
