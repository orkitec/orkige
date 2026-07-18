#include <OgreUnifiedShader.h>

// separable Gaussian blur (V), 11-tap stddev 2.0, over the downsampled
// bright-pass buffer
SAMPLER2D(Blur0, 0);

MAIN_PARAMETERS
IN(vec2 oUv, TEXCOORD0)
MAIN_DECLARATION
{
	float w[11];
	w[0]=0.01222447; w[1]=0.02783468; w[2]=0.06559061; w[3]=0.12097757;
	w[4]=0.17466632; w[5]=0.19741265; w[6]=0.17466632; w[7]=0.12097757;
	w[8]=0.06559061; w[9]=0.02783468; w[10]=0.01222447;
	vec4 sum = vec4(0.0, 0.0, 0.0, 0.0);
	for(int i = 0; i < 11; ++i)
	{
		float o = (float(i) - 5.0) * 0.01;
		vec2 uv = oUv;
			uv.y += o;
		sum += texture2D(Blur0, uv) * w[i];
	}
	gl_FragColor = sum;
}
