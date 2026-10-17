#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

// Tonemapped buffer and SSAO buffer. The samplers are separate descriptors
// because nvrhi's Vulkan backend cannot bind a combined image sampler.
layout(binding = 0) uniform texture2D u_TonemappedBuffer;
layout(binding = 1) uniform texture2D u_SSAOBuffer;

layout(binding = 2) uniform sampler u_FinalSampler;

layout(binding = 3) uniform FinalParams
{
    float u_SSAOIntensity;
} u_Final;

vec3 sampleFinal(vec2 uv)
{
    vec3 color = texture(sampler2D(u_TonemappedBuffer, u_FinalSampler), uv).rgb;
    float ssao = texture(sampler2D(u_SSAOBuffer, u_FinalSampler), uv).r;
    return color * mix(1.0, ssao, u_Final.u_SSAOIntensity);
}

float luma(vec3 color)
{
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 antialiasFinal(vec2 uv)
{
    vec2 texel = 1.0 / vec2(textureSize(sampler2D(u_TonemappedBuffer, u_FinalSampler), 0));

    vec3 center = sampleFinal(uv);
    vec3 n0 = sampleFinal(uv + vec2(texel.x, texel.y));
    vec3 n1 = sampleFinal(uv + vec2(-texel.x, texel.y));
    vec3 n2 = sampleFinal(uv + vec2(texel.x, -texel.y));
    vec3 n3 = sampleFinal(uv + vec2(-texel.x, -texel.y));

    vec3 average = (n0 + n1 + n2 + n3) * 0.25;

    float centerLuma = luma(center);
    float l0 = luma(n0);
    float l1 = luma(n1);
    float l2 = luma(n2);
    float l3 = luma(n3);

    float localMax = max(max(l0, l1), max(l2, l3));
    float localMin = min(min(l0, l1), min(l2, l3));
    float localContrast = max(abs(localMax - centerLuma), abs(centerLuma - localMin));

    if (localContrast > 0.05)
        return mix(center, average, 0.5);

    return center;
}

void main()
{
    vec3 color = antialiasFinal(vUV);
    oColor = vec4(color, 1.0);
}
