#version 450
#extension GL_EXT_scalar_block_layout : require

// Bloom step 3: 16-tap golden-spiral disc blur. Tap i sits at
// radius sqrt((i+0.5)/16) and angle 2*pi*i/phi - the disc-fill pattern
// from Forge::BloomKernel::BlurOffsets (the CPU copy defines it and
// BloomTests checks its invariants). The linear sampler supplies the 17th
// sample: every tap lands between texels, so each tap is itself a bilinear
// pair, which is what makes 16 taps read as a wide disc.

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

layout(binding = 0) uniform texture2D u_Source;
layout(binding = 1) uniform sampler u_Sampler;

layout(binding = 2) uniform BlurParams
{
    vec2 u_TexelSize;   // 1 / source dimensions
    float u_Radius;     // iteration radius in texels
} u_Blur;

const float PHI = 1.6180339887498948482;
const float TAU = 6.2831853071795864769;

void main()
{
    vec2 scale = u_Blur.u_TexelSize * u_Blur.u_Radius;

    vec3 color = texture(sampler2D(u_Source, u_Sampler), vUV).rgb;

    for (int i = 0; i < 16; ++i)
    {
        float radius = sqrt((float(i) + 0.5) / 16.0);
        float angle = TAU * float(i) / PHI;
        vec2 tap = vec2(cos(angle), sin(angle)) * radius * scale;
        color += texture(sampler2D(u_Source, u_Sampler), vUV + tap).rgb;
    }

    oColor = vec4(color / 17.0, 1.0);
}
