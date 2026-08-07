//-------------------------------
// Full-screen DISPLAY -> LINEAR decode of the opaque scene-colour target (Metal),
// so the refractive water samples the scene at its true radiance. The squaring is
// the exact inverse of the PBS shader's sqrt write into a non-sRGB target - the
// walkthrough lives in the GLSL sibling (RefractionLinearize_ps.glsl).
//-------------------------------

#include <metal_stdlib>
using namespace metal;

struct PS_INPUT
{
	float2 uv0;
};

fragment float4 main_metal
(
	PS_INPUT inPs [[stage_in]],
	texture2d<float>	Source			[[texture(0)]],
	sampler				samplerState	[[sampler(0)]]
)
{
	float4 source = Source.sample( samplerState, inPs.uv0 );
	return float4( source.rgb * source.rgb, source.a );
}
