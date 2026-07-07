Overlay of the upstream vcpkg `ogre` port (14.5.2) that adds a `metal` feature
(`OGRE_BUILD_RENDERSYSTEM_METAL=ON`, Apple platforms) so RenderSystem_Metal is
available next to GL3Plus - the upstream port has no way to enable it. Enabled
from the root `vcpkg.json`; wired via `VCPKG_OVERLAY_PORTS` in CMakePresets.json.
Delete this overlay if upstream ever grows an equivalent feature.
