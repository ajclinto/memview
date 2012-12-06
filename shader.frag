#version 130
#extension GL_EXT_gpu_shader4 : enable

in mediump vec2 texc;
out vec4 frag_color;

uniform usampler2D theState;

uniform int theTime;
uniform int theStale;
uniform int theHalfLife;

float luminance(vec3 val)
{
    return 0.3*val.r + 0.6*val.g + 0.1*val.b;
}

vec3 ramp_color(vec3 hi, vec3 lo, float interp)
{
    float lcutoff = 0.47;
    float hcutoff = 0.90;
    vec3 vals[4];

    vals[0] = lo * (0.02 / luminance(lo));
    vals[1] = lo * (0.15 / luminance(lo));
    vals[2] = hi * (0.5 / luminance(hi));
    vals[3] = hi * (2.0 / luminance(hi));

    vec3 val;
    if (interp >= hcutoff)
	val = mix(vals[2], vals[3], (interp-hcutoff)/(1-hcutoff));
    else if (interp >= lcutoff)
	val = mix(vals[1], vals[2], (interp-lcutoff)/(hcutoff-lcutoff));
    else
	val = mix(vals[0], vals[1], interp/lcutoff);
    return val;
}

void main(void)
{
    uint val = texture(theState, texc).r;
    if (val == 0u)
    {
	frag_color = vec4(0, 0, 0, 1);
	return;
    }

    uint type = val >> 30u;
    bool freed = (val & (1u << 29)) > 0u;

    int ival = int(val & ~(7u << 29));

    int diff = ival == theStale ? theHalfLife :
	((theTime > ival) ? theTime-ival+1 : ival-theTime+1);

    float interp = 1-log2(float(diff))/32;

    vec3 hi[4];
    hi[0] = vec3(0.2, 1.0, 0.2);
    hi[1] = vec3(1.0, 0.7, 0.2);
    hi[2] = vec3(0.3, 0.2, 0.8);
    hi[3] = vec3(0.3, 0.3, 0.3);

    vec3 lo[4];
    lo[0] = vec3(0.1, 0.1, 0.5);
    lo[1] = vec3(0.3, 0.1, 0.1);
    lo[2] = vec3(0.3, 0.1, 0.4);
    lo[3] = vec3(0.1, 0.1, 0.1);

    frag_color = vec4(ramp_color(hi[type], lo[type], interp), 1);

    if (freed)
	frag_color *= 0.5;
}
