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
// desktop truth (GL3Plus - what the selfcheck boots): a grab-pass RenderTexture
// sampled at a normal-perturbed screen UV. Also true on a GLES/WebGL context
// with GLSL ES 3.0 (WebGL2/GLES3 - the ES-300 water variant); the GLES2/WebGL1
// floor answers false (byte-stable Stage-1 look), as does a Vulkan/Metal context
// (pending its shader variant + an on-device proof)
X(ScreenSpaceRefraction, true)
// desktop truth (GL3Plus/Vulkan - what the selfcheck boots); a GLES2/WebGL
// context without GLSL ES 3.0 answers false at runtime
X(IblReflections, true)
// desktop truth (GL3Plus - what the selfcheck boots): a mirror-camera reflection
// RenderTexture sampled at the ripple-perturbed screen UV. Also true on a
// GLES/WebGL context with GLSL ES 3.0 (WebGL2/GLES3 - the ES-300 water variant);
// the GLES2/WebGL1 floor answers false (byte-stable), as does a Vulkan/Metal
// context (pending its shader variant + an on-device proof)
X(PlanarReflection, true)
