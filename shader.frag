#version 130
#extension GL_EXT_gpu_shader4 : enable

in mediump vec2 texc;
out vec4 frag_color;

uniform isampler2D tex;

uniform sampler1D theILut;
uniform sampler1D theRLut;
uniform sampler1D theWLut;
uniform sampler1D theALut;

uniform int theTime;
uniform int theLutBits;
uniform int theStale;
uniform int theHalfLife;

void main(void)
{
    int val = texture(tex, texc).r;
    if (val == 0)
    {
	frag_color = vec4(0, 0, 0, 1);
	return;
    }

    int diff = val == theStale ? theHalfLife :
	((theTime > val) ? theTime-val+1 : val-theTime+1);

    // Count leading zeros
    int bits = 0;
    int tmp = diff;
    while (tmp > 0)
    {
	tmp >>= 1;
	bits++;
    }
    bits = 32 - bits;

    int frac_bits = theLutBits-5;
    int thresh = 31-frac_bits;

    int clr = bits << frac_bits;

    if (bits > thresh)
	diff <<= bits - thresh;
    else
	diff >>= thresh - bits;

    clr |= (~diff) & ((1 << frac_bits)-1);

    //frag_color = texture(theRLut, texc.x);
    //frag_color = vec4(textureSize(theRLut, 0), 0, 0, 1);
    frag_color = texelFetch(theRLut, clr, 0);
    //frag_color = vec4(float(clr)/1024, 0, 0, 1);

    //frag_color = vec4(texc.x, texc.y, 0, 1);
}
