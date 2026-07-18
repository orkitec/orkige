//-------------------------------
// LDR bloom bright-pass (Metal): a luminance high-pass with a controllable
// Threshold - extracts the near-white highlights of the clamped [0;1] scene.
//-------------------------------

#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
	float2 uv0;
};

struct Params
{
	float Threshold;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	RT				[[texture(0)]],
	sampler				samplerState	[[sampler(0)]],
	constant Params &params [[buffer(PARAMETER_SLOT)]]
)
{
	float4 col = RT.sample( samplerState, inPs.uv0 );
	float lum = dot( col.rgb, float3( 0.299, 0.587, 0.114 ) );
	float factor = clamp( (lum - params.Threshold) /
		max( 1.0 - params.Threshold, 0.0001 ), 0.0, 1.0 );
	return float4( col.rgb * factor, 1.0 );
}
