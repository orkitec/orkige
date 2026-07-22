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
// the native HlmsPbs planar-reflection subsystem (Ogre::PlanarReflections)
// renders a correct mirror RTT, but its projective SAMPLING reads the wrong
// region on Metal (a uniform off-hue mirror instead of the scene), so the
// bit answers FALSE until the sampling is fixed and probe-verified - the
// subsystem + wiring stay behind the gate (diagnose with
// ORKIGE_DUMP_MIRROR=<png>); classic's mirror-camera path is the working
// planar reflection today
X(PlanarReflection, false)
