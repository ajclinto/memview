in vec2 pos;

void main(void)
{
    gl_Position = vec4(pos, -1, 1);
}
