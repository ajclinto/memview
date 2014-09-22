varying mediump vec2 texc;

void main(void)
{
    gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;
    texc = vec2(gl_MultiTexCoord0);
}
