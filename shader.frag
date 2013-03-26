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

uniform int theImageResX;
uniform int theImageResY;

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

// This routine overlays a grid pattern on the pixels when the texture is
// zoomed.  It analytically antialiases the pattern when the grid cells are
// small to avoid over-scaling.
vec4 round_block(vec4 clr, ivec2 texsize)
{
    ivec2	imgsize = ivec2(theImageResX, theImageResY);
    if (imgsize.x > texsize.x)
    {
	float bsize = sqrt(
		(imgsize.x / float(texsize.x)) *
		(imgsize.y / float(texsize.y)));

	ivec2 boff;
	boff = ivec2(vec2(texsize*imgsize)*texc);
	boff %= imgsize;
	boff /= texsize;

	// Determine the portion of pixels on the edge
	float all_pixels = bsize*bsize;
	float in_pixels = (bsize-1)*(bsize-1);
	float out_pixels = all_pixels - in_pixels;

	float max_scale = 1.25;
	float in_scale = min(all_pixels / in_pixels, max_scale);

	if (boff.x == 0 || boff.y == 0)
	{
	    float out_scale = max(1 - (in_scale - 1) *
		    (in_pixels / out_pixels), 0);
	    clr *= out_scale;
	}
	else
	{
	    clr *= in_scale;

	    // For large enough block sizes, scale the value based on the
	    // distance to the center to give it a rounded appearance.
	    // This transformation does not preserve average luminance like
	    // the grid scaling above, so don't use it for too small
	    // blocks.
	    if (bsize > 6)
	    {
		float dist = length(2*vec2(boff) - vec2(bsize+0.5));
		dist /= bsize;
		clr *= 1.25-0.5*dist;
	    }
	}
    }

    return clr;
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

    frag_color = round_block(frag_color, texsize);

    // Poor man's dithering
    vec3 rval = vec3(rand(texc)-0.5,
		     rand(texc+vec2(0.1, 0.1))-0.5,
		     rand(texc+vec2(0.2, 0.2))-0.5);
    rval *= 1.0/255.0;
    frag_color += vec4(rval.r, rval.g, rval.b, 0);
}
