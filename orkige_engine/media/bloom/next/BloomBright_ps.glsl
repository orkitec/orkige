#version ogre_glsl_ver_330

//-------------------------------
// LDR bloom bright-pass: a luminance high-pass with a controllable Threshold.
// The scene renders to a clamped [0;1] target, so this extracts the near-white
// highlights (luminance above Threshold) and scales the colour by how far past
// the cutoff it sits. Nothing above 1.0 exists to bloom here (no HDR yet).
//-------------------------------

vulkan_layout( ogre_t0 ) uniform texture2D RT;
vulkan( layout( ogre_s0 ) uniform sampler samplerState );

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform float Threshold;
vulkan( }; )

vulkan_layout( location = 0 )
out vec4 fragColour;

vulkan_layout( location = 0 )
in block
{
	vec2 uv0;
} inPs;

void main()
{
	vec4 col = texture( vkSampler2D( RT, samplerState ), inPs.uv0 );
	float lum = dot( col.rgb, vec3( 0.299, 0.587, 0.114 ) );
	float factor = clamp( (lum - Threshold) / max( 1.0 - Threshold, 0.0001 ),
		0.0, 1.0 );
	fragColour = vec4( col.rgb * factor, 1.0 );
}
