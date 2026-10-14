#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec2 vUV;
layout(location = 0) out float oSSAO;

// Depth texture (combined sampler).
layout(binding = 0) uniform sampler2D u_DepthSampler;

layout(binding = 1) uniform SSAOParams
{
    mat4 u_InvViewProjection;
    float u_Intensity;
    float u_Radius;
    float u_Bias;
    float u_Power;
    vec2 u_TexelSize;
} u_SSAO;

const int SAMPLE_COUNT = 32;
const float PI = 3.14159265359;

// 32 hemisphere sample offsets (cosine-ish distributed, unit length).
const vec3 kKernel[32] = vec3[32](
    vec3( 0.5381,  0.1856, 0.4319), vec3( 0.1379,  0.2486, 0.4430),
    vec3( 0.3371,  0.5679, 0.0057), vec3(-0.6999, -0.0451, 0.0019),
    vec3( 0.0689, -0.1598, 0.8547), vec3( 0.0560,  0.0069, 0.1843),
    vec3(-0.0146,  0.1402, 0.0762), vec3( 0.0100, -0.1924, 0.0344),
    vec3(-0.3577, -0.5301, 0.4358), vec3(-0.3169,  0.1063, 0.0158),
    vec3( 0.0103, -0.5869, 0.0046), vec3(-0.0897, -0.4940, 0.3287),
    vec3( 0.7119, -0.0154, 0.0918), vec3(-0.0533,  0.0596, 0.5411),
    vec3( 0.0352, -0.0631, 0.5460), vec3(-0.4776,  0.2847, 0.0271),
    vec3(-0.0848,  0.3003, 0.4584), vec3( 0.4883, -0.0478, 0.1084),
    vec3( 0.4700,  0.2038, 0.7443), vec3(-0.0258,  0.3564, 0.4537),
    vec3( 0.0994, -0.3081, 0.1997), vec3( 0.0817,  0.1460, 0.3776),
    vec3( 0.2126, -0.2137, 0.0927), vec3(-0.2377, -0.2878, 0.1682),
    vec3(-0.1534,  0.0066, 0.4479), vec3(-0.1191, -0.4154, 0.3238),
    vec3( 0.3700,  0.0849, 0.5181), vec3( 0.0378, -0.3818, 0.2197),
    vec3(-0.1304,  0.0434, 0.4367), vec3( 0.0612, -0.0386, 0.2777),
    vec3(-0.0241,  0.4695, 0.1200), vec3(-0.4154,  0.1033, 0.2312)
);

// Reconstruct view-space position from depth (NDC -> View).
vec3 ReconstructViewPosition(vec2 uv, float depth)
{
    vec4 clipPos = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 viewPos = u_SSAO.u_InvViewProjection * clipPos;
    return viewPos.xyz / viewPos.w;
}

// Reconstruct view-space normal from depth derivatives.
vec3 ReconstructViewNormal(vec2 uv, float depth)
{
    vec2 texelSize = u_SSAO.u_TexelSize;
    float depthX = texture(u_DepthSampler, uv + vec2(texelSize.x, 0.0)).r;
    float depthY = texture(u_DepthSampler, uv + vec2(0.0, texelSize.y)).r;

    vec3 pos = ReconstructViewPosition(uv, depth);
    vec3 posX = ReconstructViewPosition(uv + vec2(texelSize.x, 0.0), depthX);
    vec3 posY = ReconstructViewPosition(uv + vec2(0.0, texelSize.y), depthY);

    vec3 dPdx = posX - pos;
    vec3 dPdy = posY - pos;
    return normalize(cross(dPdy, dPdx));
}

void main()
{
    float depth = texture(u_DepthSampler, vUV).r;

    // Skip background (far plane).
    if (depth >= 1.0)
    {
        oSSAO = 1.0;
        return;
    }

    vec3 position = ReconstructViewPosition(vUV, depth);
    vec3 normal = ReconstructViewNormal(vUV, depth);

    // Build a random orthonormal tangent frame around the normal.
    vec3 randomVec = normalize(vec3(
        fract(sin(gl_FragCoord.x) * 43758.5453),
        fract(sin(gl_FragCoord.y) * 22578.1459),
        fract(sin(gl_FragCoord.x + gl_FragCoord.y) * 12345.6789)
    ));

    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        vec3 samplePos = position + (TBN * kKernel[i]) * u_SSAO.u_Radius;
        vec2 sampleUV = vUV + (TBN * kKernel[i]).xy * u_SSAO.u_Radius;

        // Clamp to texture bounds.
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 || sampleUV.y < 0.0 || sampleUV.y > 1.0)
            continue;

        float sampleDepth = texture(u_DepthSampler, sampleUV).r;

        // Reconstruct the sample's view-space position.
        vec3 sampleViewPos = ReconstructViewPosition(sampleUV, sampleDepth);

        // Range check to avoid false occlusion at depth discontinuities.
        float rangeCheck = smoothstep(0.0, 1.0, u_SSAO.u_Radius / max(abs(position.z - sampleViewPos.z), 0.0001));

        // Occlusion if sample is behind the surface (with bias).
        occlusion += (sampleViewPos.z >= samplePos.z + u_SSAO.u_Bias ? 0.0 : 1.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / float(SAMPLE_COUNT));
    oSSAO = clamp(pow(occlusion, u_SSAO.u_Power) * u_SSAO.u_Intensity, 0.0, 1.0);
}
