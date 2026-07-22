//-------------------------------
// Output grade (Metal): the shared contrast (S-curve) + saturation look applied
// to the off-screen 3D scene texture, the exact core_util/GradeMath transform.
// The scene texture is sRGB (sample decodes to linear); the grade is authored in
// display space, so encode to display, grade, then decode back to linear for the
// sRGB window store (@see GradeApply_ps.glsl for the colour-space contract).
//-------------------------------

#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
	float2 uv0;
};

struct Params
{
	float Contrast;
	float Saturation;
};

static float3 linearToDisplay(float3 c)
{
	float3 lo = c * 12.92;
	float3 hi = 1.055 * pow(max(c, 0.0), float3(1.0 / 2.4)) - 0.055;
	return select(lo, hi, c >= float3(0.0031308));
}

static float3 displayToLinear(float3 c)
{
	float3 lo = c / 12.92;
	float3 hi = pow((max(c, 0.0) + 0.055) / 1.055, float3(2.4));
	return select(lo, hi, c >= float3(0.04045));
}

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	RT				[[texture(0)]],
	sampler				samplerState	[[sampler(0)]],
	constant Params &params [[buffer(PARAMETER_SLOT)]]
)
{
	float3 lin = RT.sample( samplerState, inPs.uv0 ).rgb;
	float3 c = clamp(linearToDisplay(lin), 0.0, 1.0);
	float3 sc = c * c * (3.0 - 2.0 * c);
	c = mix(c, sc, params.Contrast);
	float luma = dot(c, float3(0.299, 0.587, 0.114));
	c = mix(float3(luma), c, params.Saturation);
	c = clamp(c, 0.0, 1.0);
	return float4(displayToLinear(c), 1.0);
}
