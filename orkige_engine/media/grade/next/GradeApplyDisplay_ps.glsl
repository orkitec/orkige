#version ogre_glsl_ver_330

//-------------------------------
// Output grade, DISPLAY-source variant (the Ogre-Next flavor): the same shared
// contrast (S-curve) + saturation curve as GradeApply_ps, but for a source
// texture that already holds DISPLAY-space colour (a non-sRGB target) - so no
// linear<->display adapter. Used where the grade composes onto the refraction
// path, whose scene/water targets are non-sRGB for byte-passthrough (@see
// RenderBackend::recreateWindowWorkspace). The curve is the exact
// core_util/GradeMath transform, matched to the classic grade quad.
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
	vec3 sc = c * c * (3.0 - 2.0 * c);
	c = mix(c, sc, Contrast);
	float luma = dot(c, vec3(0.299, 0.587, 0.114));
	c = mix(vec3(luma), c, Saturation);
	fragColour = vec4(clamp(c, 0.0, 1.0), 1.0);
}
