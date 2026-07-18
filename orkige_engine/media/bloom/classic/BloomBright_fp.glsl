#include <OgreUnifiedShader.h>

// LDR bright-pass: a luminance high-pass with a controllable Threshold.
SAMPLER2D(RT, 0);

OGRE_UNIFORMS(
	uniform float Threshold;
)

MAIN_PARAMETERS
IN(vec2 oUv, TEXCOORD0)
MAIN_DECLARATION
{
	vec4 col = texture2D(RT, oUv);
	float lum = dot(col.rgb, vec3(0.299, 0.587, 0.114));
	float factor = clamp((lum - Threshold) / max(1.0 - Threshold, 0.0001),
		0.0, 1.0);
	gl_FragColor = vec4(col.rgb * factor, 1.0);
}
