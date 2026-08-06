//-------------------------------
// Output grade (Metal): the shared contrast (S-curve) + saturation look applied
// to the off-screen 3D scene texture, the exact core_util/GradeMath transform.
// The source target is non-sRGB and carries DISPLAY-space colour, so the curve
// runs directly on the sampled value with no linear<->display adapter
// (@see GradeApply_ps.glsl for the colour-space contract).
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

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	RT				[[texture(0)]],
	sampler				samplerState	[[sampler(0)]],
	constant Params &params [[buffer(PARAMETER_SLOT)]]
)
{
	float3 c = clamp(RT.sample( samplerState, inPs.uv0 ).rgb, 0.0, 1.0);
	float3 sc = c * c * (3.0 - 2.0 * c);
	c = mix(c, sc, params.Contrast);
	float luma = dot(c, float3(0.299, 0.587, 0.114));
	c = mix(float3(luma), c, params.Saturation);
	return float4(clamp(c, 0.0, 1.0), 1.0);
}
