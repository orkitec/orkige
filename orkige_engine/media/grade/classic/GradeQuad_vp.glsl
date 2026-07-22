#include <OgreUnifiedShader.h>

// fullscreen-quad passthrough vertex program for the output-grade compositor
// pass (the classic OGRE flavor). Same recipe as the bloom quad VS; the grade
// media ships its own so the grade compositor resolves without depending on the
// bloom media being registered.
OGRE_UNIFORMS(
	uniform mat4 worldViewProj;
)

MAIN_PARAMETERS
IN(vec4 vertex, POSITION)
IN(vec2 uv0, TEXCOORD0)
OUT(vec2 oUv, TEXCOORD0)
MAIN_DECLARATION
{
	gl_Position = mul(worldViewProj, vertex);
	oUv = uv0;
}
