#include <OgreUnifiedShader.h>

// additive combine: the sharp scene plus the blurred highlights * Intensity.
SAMPLER2D(RT, 0);
SAMPLER2D(Blur1, 1);

OGRE_UNIFORMS(
	uniform float OriginalImageWeight;
	uniform float Intensity;
)

MAIN_PARAMETERS
IN(vec2 oUv, TEXCOORD0)
MAIN_DECLARATION
{
	vec4 sharp = texture2D(RT, oUv);
	vec4 blur  = texture2D(Blur1, oUv);
	gl_FragColor = vec4((sharp.rgb * OriginalImageWeight) +
		(blur.rgb * Intensity), 1.0);
}
