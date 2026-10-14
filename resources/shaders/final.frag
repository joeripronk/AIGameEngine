#version 450

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(binding = 0, set = 0) uniform sampler2D u_TonemappedBuffer;
layout(binding = 1, set = 0) uniform sampler2D u_SSAOBuffer;

layout(binding = 2, set = 0) uniform FinalParams
{
    float u_SSAOIntensity;
} u_Final;

void main()
{
    vec3 color = texture(u_TonemappedBuffer, vUV).rgb;
    float ssao = texture(u_SSAOBuffer, vUV).r;
    color *= mix(1.0, ssao, u_Final.u_SSAOIntensity);
    oColor = vec4(color, 1.0);
}