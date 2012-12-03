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

    float clr = 1-log2(float(diff))/32;

    frag_color = texture(theRLut, clr);
}
