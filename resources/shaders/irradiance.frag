#version 450
#extension GL_EXT_scalar_block_layout : require

#define PI 3.14159265358979323846

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 oColor;

// 2D equirectangular environment map using combined sampler.
layout(binding = 0) uniform sampler2D u_EnvMap;

layout(binding = 1) uniform IrradianceParams
{
    vec3 u_N;
    float u_Radius;
} u_Irr;

const int SAMPLE_COUNT = 64;

// Convert a 3D direction to equirectangular UV coordinates.
vec2 DirToEquirectangularUV(vec3 dir)
{
    float u = atan(dir.z, dir.x) / (2.0 * PI) + 0.5;
    float v = acos(dir.y) / PI;
    return vec2(u, v);
}

// Generate 64 sample directions evenly distributed on a hemisphere.
vec3 SampleDirection(int sampleIndex)
{
    float phi = 2.0 * PI * float(sampleIndex) / float(SAMPLE_COUNT);
    float theta = sqrt(1.0 - (float(sampleIndex) / float(SAMPLE_COUNT)) * 2.0);
    return vec3(cos(phi) * theta, sin(phi) * theta, sqrt(1.0 - theta * theta));
}

void main()
{
    vec3 N = u_Irr.u_N;
    vec3 irradiance = vec3(0.0);

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        vec3 sampleDir = SampleDirection(i);
        float NdotL = dot(N, sampleDir);

        if (NdotL > 0.0)
        {
            vec3 envColor = texture(u_EnvMap, DirToEquirectangularUV(sampleDir)).rgb;
            irradiance += envColor * NdotL;
        }
    }

    irradiance /= float(SAMPLE_COUNT);
    oColor = vec4(irradiance, 1.0);
}
