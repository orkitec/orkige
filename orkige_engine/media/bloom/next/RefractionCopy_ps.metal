//-------------------------------
// Full-screen colour copy (Metal): sample the source render target and write it
// verbatim. The water screen-space-refraction workspace uses it to resolve the
// composited scene-colour target onto the window (@see
// RenderBackend::recreateWindowWorkspace).
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
	return Source.sample( samplerState, inPs.uv0 );
}
