#version 130
#extension GL_EXT_gpu_shader4 : enable

in mediump vec2 texc;
out vec4 frag_color;

uniform usampler2DRect theState;
uniform sampler1D theColors;

uniform int theTime;
uniform int theStale;
uniform int theHalfLife;

uniform int theDisplayMode;
uniform int theDisplayStack;

float rand(vec2 co)
{
    return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453);
}

float luminance(vec3 val)
{
    return dot(val, vec3(0.3, 0.59, 0.11));
}

vec3 lum1(vec3 val)
{
    return val / luminance(val);
}

vec3 ramp_color(vec3 hi, vec3 lo, float interp)
{
    float lcutoff = 0.6;
    float hcutoff = 0.95;
    vec3 vals[4];

    vals[0] = lo * 0.02;
    vals[1] = lo * 0.15;
    vals[2] = hi * 0.7;
    vals[3] = hi * 2.0;

    if (interp >= hcutoff)
	return mix(vals[2], vals[3], (interp-hcutoff)/(1-hcutoff));
    else if (interp >= lcutoff)
	return mix(vals[1], vals[2], (interp-lcutoff)/(hcutoff-lcutoff));
    return mix(vals[0], vals[1], interp/lcutoff);
}

void main(void)
{
    ivec2   texsize = textureSize(theState);
    uint    val = texture(theState, texsize*texc).r;

    if (val == 0u)
    {
	frag_color = vec4(0, 0, 0, 1);
	return;
    }

    uint dtype = val & 7u;
    uint type = (val >> 3u) & 3u;
    bool freed = ((val >> 3u) & 4u) > 0u;
    uint tid = (val >> 6u) & 0x3FFu;
    bool hasstack = (val & (1u << 16u)) > 0u;

    int ival = int(val >> 17u);

    int diff = ival == theStale ? theHalfLife :
	((theTime > ival) ? theTime-ival+1 : ival-theTime+1);

    float interp = 1-log2(float(diff))/32;

    if (theDisplayMode == 1)
    {
	int size = textureSize(theColors, 0);

	vec3 clr = lum1(vec3(texture(theColors, (float(tid)+0.5)/size)));
	frag_color = vec4(ramp_color(clr, clr, interp), 1);
    }
    else if (theDisplayMode == 2)
    {
	int size = textureSize(theColors, 0);

	vec3 clr = lum1(vec3(texture(theColors, (float(dtype)+0.5)/size)));
	frag_color = vec4(ramp_color(clr, clr, interp), 1);
    }
    else
    {
	vec3 hi[4];
	hi[0] = lum1(vec3(0.3, 0.3, 0.3));
	hi[1] = lum1(vec3(0.3, 0.2, 0.8));
	hi[2] = lum1(vec3(1.0, 0.7, 0.2));
	hi[3] = lum1(vec3(0.2, 1.0, 0.2));

	vec3 lo[4];
	lo[0] = lum1(vec3(0.1, 0.1, 0.1));
	lo[1] = lum1(vec3(0.3, 0.1, 0.4));
	lo[2] = lum1(vec3(0.3, 0.1, 0.1));
	lo[3] = lum1(vec3(0.1, 0.1, 0.5));

	frag_color = vec4(ramp_color(hi[type], lo[type], interp), 1);
    }

    if (freed)
	frag_color *= 0.5;

    if (theDisplayStack > 0)
    {
	if (hasstack)
	    frag_color = vec4(1, 1, 0.5, 1);
	else
	{
	    // Change to grayscale
	    frag_color = vec4(luminance(vec3(frag_color))*0.25);
	    frag_color = min(frag_color, vec4(0.25, 0.25, 0.25, 1));
	}
    }

    // Poor man's dithering
    vec3 rval = vec3(rand(texc)-0.5,
		     rand(texc+vec2(0.1, 0.1))-0.5,
		     rand(texc+vec2(0.2, 0.2))-0.5);
    rval *= 1.0/255.0;
    frag_color += vec4(rval.r, rval.g, rval.b, 0);
}
