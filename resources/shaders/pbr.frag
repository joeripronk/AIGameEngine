#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 vWorldPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

layout(location = 0) out vec4 oColor;

// Set 0, binding 0: per-frame data.
layout(binding = 0) uniform CameraData
{
    mat4 u_ViewProjection;
    mat4 u_ShadowViewProjection;
    vec4 u_LightDirection;
    vec4 u_LightColor;
    vec4 u_CameraPosition;
    vec4 u_AmbientLight;
    vec4 u_SSAOParams;
} u_Camera;

// Set 0, binding 1: per-object data.
layout(binding = 1) uniform ObjectData
{
    mat4 u_Transform;
    mat4 u_NormalMatrix;
    vec4 u_Albedo;
    vec4 u_MetallicRoughnessAO;
    vec4 u_Emissive;
} u_Object;

// Set 0, binding 2: material albedo map (combined sampler).
layout(binding = 2) uniform sampler2D u_AlbedoTexture;

// Set 0, binding 3: shadow map (combined sampler).
layout(binding = 3) uniform sampler2D u_ShadowMap;

// Set 0, binding 4: linear sampler for albedo.
layout(binding = 4) uniform sampler u_LinearSampler;

// Shadow map dimensions (used for PCF).
layout(binding = 5) uniform ShadowMapParams
{
    vec2 u_ShadowMapSize;
} u_Shadow;

// Set 0, binding 6: environment map (cube map for IBL, combined sampler).
layout(binding = 6) uniform samplerCube u_EnvMap;

const float PI = 3.14159265359;

float DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    return GeometrySchlickGGX(max(dot(N, V), 0.0), roughness)
         * GeometrySchlickGGX(max(dot(N, L), 0.0), roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// 3x3 percentage-closer filtering for a soft shadow edge.
float SampleShadow(vec3 worldPosition)
{
    vec4 lightClip = u_Camera.u_ShadowViewProjection * vec4(worldPosition, 1.0);

    if (lightClip.w <= 0.0)
        return 1.0;

    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    float bias = u_Camera.u_SSAOParams.x;

    vec2 texel = 1.0 / u_Shadow.u_ShadowMapSize;
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float depth = texture(u_ShadowMap, proj.xy + vec2(float(x), float(y)) * texel).r;
            shadow += (proj.z - bias > depth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// IBL: Sample the environment map for specular reflections.
vec3 SampleEnvSpecular(vec3 R, float roughness)
{
    // Use the roughness to select the mip level for the environment map.
    float mipLevel = roughness * (6.0 - 1.0) + 1.0;
    mipLevel = clamp(mipLevel, 1.0, 6.0);

    vec3 envColor = texture(u_EnvMap, R, mipLevel).rgb;
    return envColor;
}

void main()
{
    vec3 albedo = u_Object.u_Albedo.rgb * texture(u_AlbedoTexture, vUV).rgb;
    float metallic = u_Object.u_MetallicRoughnessAO.x;
    float roughness = clamp(u_Object.u_MetallicRoughnessAO.y, 0.04, 1.0);
    float ao = u_Object.u_MetallicRoughnessAO.z;
    vec3 emissive = u_Object.u_Emissive.rgb * u_Object.u_Emissive.a;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(u_Camera.u_CameraPosition.xyz - vWorldPosition);
    vec3 L = normalize(u_Camera.u_LightDirection.xyz);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 specular = (NDF * G * F) / max(4.0 * NdotV * NdotL, 0.0001);

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    float shadow = SampleShadow(vWorldPosition);

    // IBL specular contribution.
    vec3 R = reflect(-V, N);
    vec3 envSpecular = SampleEnvSpecular(R, roughness);
    vec3 envSpecularContrib = envSpecular * F;

    vec3 radiance = (diffuse + specular) * u_Camera.u_LightColor.rgb * u_Camera.u_LightColor.a;
    vec3 ambient = u_Camera.u_AmbientLight.rgb * albedo * ao;

    vec3 color = ambient + radiance * NdotL * shadow + envSpecularContrib + emissive;

    oColor = vec4(color, 1.0);
}
