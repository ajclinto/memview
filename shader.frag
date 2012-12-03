#version 130
#extension GL_EXT_gpu_shader4 : enable

in mediump vec2 texc;
out vec4 frag_color;

uniform usampler2D theState;

uniform sampler1D theRLut;
uniform sampler1D theWLut;
uniform sampler1D theILut;
uniform sampler1D theALut;

uniform int theTime;
uniform int theStale;
uniform int theHalfLife;

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

    float clr = 1-log2(float(diff))/32;

    if (type == 0u)
	frag_color = texture(theRLut, clr);
    else if (type == 1u)
	frag_color = texture(theWLut, clr);
    else if (type == 2u)
	frag_color = texture(theILut, clr);
    else
	frag_color = texture(theALut, clr);

    if (freed)
	frag_color *= 0.5;
}
