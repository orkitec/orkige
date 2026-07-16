// The classic render backend's expected capability snapshot: one line per
// RenderCaps identity, X(EnumIdentifier, bool). This is the committed mirror of
// the live boot fill in ClassicBackend.cpp (createRenderSystem) - the register
// leg of render_facade_selfcheck asserts RenderSystem::supports() matches THIS,
// so a boot-fill change without a matching edit here fails the test. It is also
// the classic column source for the generated matrix in
// Docs/render-abstraction.md (parsed by Util/update_docs.py).
// Regenerate the matrix after any edit: python3 Util/update_docs.py --write
X(SkyDome, true)
X(DynamicShadows, false)
X(HemisphereAmbient, false)
X(SunExposureLinkage, false)
X(AnimatedNormalMappedWater, false)
X(OffscreenOwnedLayers, false)
X(ScreenSpaceRefraction, false)
X(IblReflections, false)
