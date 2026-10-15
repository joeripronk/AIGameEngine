#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

// HDR buffer. The sampler is a separate descriptor because nvrhi's Vulkan
// backend cannot bind a combined image sampler.
layout(binding = 0) uniform texture2D u_HDRBuffer;

layout(binding = 1) uniform sampler u_HDRSampler;

layout(binding = 2) uniform HDRParams
{
    float u_Exposure;
} u_HDR;

const float PI = 3.14159265359;

// ACES filmic tonemapping (Narkowicz approximation).
vec3 ACESFilm(vec3 x)
{
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

void main()
{
    vec3 hdrColor = texture(sampler2D(u_HDRBuffer, u_HDRSampler), vUV).rgb;
    hdrColor *= u_HDR.u_Exposure;

    // ACES tonemap then gamma-correct to sRGB.
    vec3 mapped = ACESFilm(hdrColor);
    mapped = pow(mapped, vec3(1.0 / 2.2));

    oColor = vec4(mapped, 1.0);
}
