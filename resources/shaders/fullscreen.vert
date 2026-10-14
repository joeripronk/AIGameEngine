#version 450

layout(location = 0) in vec2 aPosition;
layout(location = 1) in vec2 aUV;
layout(location = 0) out vec2 vUV;

void main()
{
    vUV = aUV;
    gl_Position = vec4(aPosition, 0.0, 1.0);
}