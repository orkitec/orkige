#version ogre_glsl_ver_330

//-------------------------------
// Full-screen colour copy: sample the source render target and write it
// verbatim. The water screen-space-refraction workspace uses it to resolve the
// composited scene-colour target onto the window (@see
// RenderBackend::recreateWindowWorkspace).
//-------------------------------

vulkan_layout( ogre_t0 ) uniform texture2D Source;

vulkan( layout( ogre_s0 ) uniform sampler samplerState );

vulkan_layout( location = 0 )
out vec4 fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

void main()
{
	fragColour = texture( vkSampler2D( Source, samplerState ), inPs.uv0 );
}
