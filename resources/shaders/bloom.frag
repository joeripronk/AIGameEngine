#version 450
#extension GL_EXT_scalar_block_layout : require

// Bloom step 1+2: luminance filter + 13-tap downsample, full res -> half
// res. The filter is the soft-threshold curve defined in
// src/Forge/Renderer/Bloom.h (BloomLumaWeight) - the CPU function is the
// definition, this is its GLSL evaluation, and BloomTests asserts its
// properties.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform texture2D u_HDRBuffer;
layout(binding = 1) uniform sampler u_HDRSampler;

layout(binding = 2) uniform BloomParams
{
    vec2 u_TexelSize;   // 1 / destination (half-res) dimensions
    float u_Threshold;
    float u_Knee;
    float u_Scale;
} u_Bloom;

const vec3 LUMA = vec3(0.2125, 0.7154, 0.0721);

// The 13-tap box from Forge::BloomKernel::DownsampleOffsets (kept in sync;
// BloomTests checks the CPU copy stays a 13-tap box in the unit square).
const vec3 kTaps[13] = vec3[13](
    vec3(-1.0, -1.0, 1.0), vec3(0.0, -1.0, 1.0), vec3(1.0, -1.0, 1.0),
    vec3(-1.0, 0.0, 1.0),  vec3(0.0, 0.0, 1.0),  vec3(1.0, 0.0, 1.0),
    vec3(-1.0, 1.0, 1.0),  vec3(0.0, 1.0, 1.0),  vec3(1.0, 1.0, 1.0),
    vec3(-1.5, -1.5, 1.0), vec3(1.5, -1.5, 1.0),
    vec3(-1.5, 1.5, 1.0),  vec3(1.5, 1.5, 1.0));

void main()
{
    vec3 color = vec3(0.0);

    for (int i = 0; i < 13; ++i)
    {
        vec2 uv = vUV + kTaps[i].xy * 0.5 * u_Bloom.u_TexelSize;
        color += texture(sampler2D(u_HDRBuffer, u_HDRSampler), uv).rgb;
    }

    color /= 13.0;

    // Soft-threshold luminance filter (Bloom.h): keep only luminance
    // excess over the threshold, normalized back to a per-texel weight so
    // the blurs cannot make bright texels crawl.
    float lum = max(dot(color, LUMA), 0.0);
    float x = max(lum - u_Bloom.u_Threshold, 0.0);
    float knee = u_Bloom.u_Threshold * u_Bloom.u_Knee;
    float soft = x - x * x / (x + knee + 1e-5);
    soft = clamp(soft, 0.0, knee);

    float weight = 0.0;
    if (lum > 1e-5)
        weight = soft / lum * u_Bloom.u_Scale;

    oColor = vec4(color * weight, 1.0);
}
