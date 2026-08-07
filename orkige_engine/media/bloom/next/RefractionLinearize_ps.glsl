#version ogre_glsl_ver_330

//-------------------------------
// Full-screen DISPLAY -> LINEAR decode of the opaque scene-colour target, so the
// refractive water samples the scene at its true radiance.
//
// The opaque scene target is not an sRGB surface, so the PBS pixel shader writes
// sqrt(linear) into it - a display-space image. The water datablock's refraction
// term adds what it samples into its LINEAR accumulator and encodes the sum once
// at the end, so handing it the display-space image encodes the transmitted
// scene TWICE (sqrt(sqrt(linear))), which reads roughly twice as bright on
// screen for a dark lakebed. Squaring here is the exact inverse of that write,
// so the water's own single encode is the only one the transmitted scene gets.
//
// @see RenderBackend::recreateWindowWorkspace (the RefractRT target pass).
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
	vec4 source = texture( vkSampler2D( Source, samplerState ), inPs.uv0 );
	fragColour = vec4( source.rgb * source.rgb, source.a );
}
