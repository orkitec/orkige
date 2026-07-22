#version ogre_glsl_ver_330

//-------------------------------
// Output grade (the Ogre-Next flavor): the shared contrast (S-curve) +
// saturation look applied to the off-screen 3D scene texture. The curve is the
// EXACT core_util/GradeMath transform, identical to the classic flavor's grade
// quad, so the authored look matches across flavors by construction.
//
// COLOUR SPACE: the scene texture is sRGB, so the sample decodes to LINEAR.
// The grade is authored in DISPLAY space (the space core_util/GradeMath and the
// classic scene texture use), so this shader encodes to display, grades, then
// decodes back to linear for the sRGB window store to re-encode - the round trip
// that leaves the graded display value on screen and matches the classic path.
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

vec3 linearToDisplay(vec3 c)
{
	return mix(c * 12.92,
		1.055 * pow(max(c, 0.0), vec3(1.0 / 2.4)) - 0.055,
		step(vec3(0.0031308), c));
}

vec3 displayToLinear(vec3 c)
{
	return mix(c / 12.92,
		pow((max(c, 0.0) + 0.055) / 1.055, vec3(2.4)),
		step(vec3(0.04045), c));
}

void main()
{
	vec3 lin = texture( vkSampler2D( RT, samplerState ), inPs.uv0 ).rgb;
	vec3 c = clamp(linearToDisplay(lin), 0.0, 1.0);
	// contrast: mix(x, smoothstep(x), Contrast) per channel (0.5 pivot)
	vec3 sc = c * c * (3.0 - 2.0 * c);
	c = mix(c, sc, Contrast);
	// saturation about the Rec.601 luma
	float luma = dot(c, vec3(0.299, 0.587, 0.114));
	c = mix(vec3(luma), c, Saturation);
	c = clamp(c, 0.0, 1.0);
	fragColour = vec4(displayToLinear(c), 1.0);
}
