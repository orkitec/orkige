#include <OgreUnifiedShader.h>

//---------------------------------------------------------------------------
// Output grade (the classic OGRE flavor): the shared contrast (S-curve) +
// saturation look, applied to the off-screen 3D scene texture. The curve is
// the EXACT core_util/GradeMath transform - identical to the next flavor's
// grade quad so the authored look matches across flavors by construction.
//
// The classic scene render target is DISPLAY-encoded (the RTSS lit path writes
// display-space colour), so this shader grades the sampled value directly with
// no colour-space adapter - the space core_util/GradeMath documents. Contrast
// and Saturation are pushed live from RenderWorld::setOutputGrade.
//---------------------------------------------------------------------------
SAMPLER2D(RT, 0);

OGRE_UNIFORMS(
	uniform float Contrast;
	uniform float Saturation;
)

MAIN_PARAMETERS
IN(vec2 oUv, TEXCOORD0)
MAIN_DECLARATION
{
	vec3 c = clamp(texture2D(RT, oUv).rgb, 0.0, 1.0);
	// contrast: mix(x, smoothstep(x), Contrast) per channel (0.5 pivot)
	vec3 sc = c * c * (3.0 - 2.0 * c);
	c = mix(c, sc, Contrast);
	// saturation about the Rec.601 luma
	float luma = dot(c, vec3(0.299, 0.587, 0.114));
	c = mix(vec3(luma), c, Saturation);
	gl_FragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
