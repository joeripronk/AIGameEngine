#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 0) out float oDepth;

void main()
{
    // Distance from the light, used for shadow comparison.
    oDepth = gl_FragCoord.z;
}