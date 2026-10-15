#version 450
#extension GL_EXT_scalar_block_layout : require

#define PI 3.14159265358979323846

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

// 2D equirectangular environment map. The sampler is a separate descriptor
// because nvrhi's Vulkan backend cannot bind a combined image sampler.
layout(binding = 0) uniform texture2D u_EnvMap;

layout(binding = 1) uniform sampler u_EnvSampler;

layout(binding = 2) uniform PrefilterParams
{
    vec3 u_R;
    float u_Roughness;
} u_Pref;

const int SAMPLE_COUNT = 1024;

// Convert a 3D direction to equirectangular UV coordinates.
vec2 DirToEquirectangularUV(vec3 dir)
{
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = acos(dir.y) / PI;
    return vec2(u, v);
}

void main()
{
    vec3 R = u_Pref.u_R;
    float roughness = u_Pref.u_Roughness;
    vec3 specular = vec3(0.0);
    float totalWeight = 0.0;

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        // Generate a random hemisphere sample around R.
        float phi = 2.0 * PI * float(i) / float(SAMPLE_COUNT);
        float theta = sqrt(float(i) / float(SAMPLE_COUNT + 1));
        vec3 tangent = vec3(cos(phi) * theta, sin(phi) * theta, sqrt(1.0 - theta * theta));

        // Rotate tangent frame to align R with +Z.
        vec3 up = abs(R.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
        vec3 tangent2 = normalize(cross(R, up));
        vec3 bitangent = cross(R, tangent2);

        vec3 sampleDir = normalize(R * tangent.z + tangent2 * tangent.x + bitangent * tangent.y);

        float NdotL = dot(R, sampleDir);
        if (NdotL > 0.0)
        {
            vec3 envColor = texture(sampler2D(u_EnvMap, u_EnvSampler), DirToEquirectangularUV(sampleDir)).rgb;
            specular += envColor * NdotL;
            totalWeight += NdotL;
        }
    }

    specular = specular / totalWeight;
    oColor = vec4(specular, 1.0);
}
