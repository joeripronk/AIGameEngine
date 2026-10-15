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

void main()
{
    vec3 color = texture(sampler2D(u_TonemappedBuffer, u_FinalSampler), vUV).rgb;
    float ssao = texture(sampler2D(u_SSAOBuffer, u_FinalSampler), vUV).r;
    color *= mix(1.0, ssao, u_Final.u_SSAOIntensity);
    oColor = vec4(color, 1.0);
}
