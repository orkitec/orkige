#version ogre_glsl_ver_330

//-------------------------------
// Output grade (the Ogre-Next flavor): the shared contrast (S-curve) +
// saturation look applied to the off-screen 3D scene texture. The curve is the
// EXACT core_util/GradeMath transform, identical to the classic flavor's grade
// quad, so the authored look matches across flavors by construction.
//
// COLOUR SPACE: every off-screen colour target this quad reads is non-sRGB and
// carries DISPLAY-space colour, because the whole next pipeline is a gamma-space
// passthrough down to the non-sRGB swapchain (the colour-parity rule - @see
// RenderBackend::recreateWindowWorkspace). Display space is the space
// core_util/GradeMath is authored in and the space the classic scene texture
// carries, so the sampled value is graded DIRECTLY, with no linear<->display
// adapter. An adapter pair around the curve cancels only while the grade is the
// identity - with a real grade it applies the SAME transform in a different
// space than the classic quad does, which is a look divergence.
//-------------------------------

vulkan_layout( ogre_t0 ) uniform texture2D RT;

vulkan( layout( ogre_s0 ) uniform sampler samplerState );

vulkan( layout( ogre_P0 ) uniform Params { )
	uniform float Contrast;
	uniform float Saturation;
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
	vec3 c = clamp(texture( vkSampler2D( RT, samplerState ), inPs.uv0 ).rgb,
		0.0, 1.0);
	// contrast: mix(x, smoothstep(x), Contrast) per channel (0.5 pivot)
	vec3 sc = c * c * (3.0 - 2.0 * c);
	c = mix(c, sc, Contrast);
	// saturation about the Rec.601 luma
	float luma = dot(c, vec3(0.299, 0.587, 0.114));
	c = mix(vec3(luma), c, Saturation);
	fragColour = vec4(clamp(c, 0.0, 1.0), 1.0);
}
