#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 vPosition;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec3 vTangent;
layout(location = 4) in vec3 vBitangent;
layout(location = 5) in vec3 vWorldPosition;

layout(location = 0) out vec4 oColor;

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

layout(binding = 1, set = 0) uniform ObjectData
{
    mat4 u_Transform;
    vec4 u_Albedo;
    vec4 u_MetallicRoughness;
    vec4 u_Emissive;
    mat4 u_NormalMatrix;
} u_Object;

layout(binding = 0, set = 1) uniform sampler2D u_AlbedoTexture;
layout(binding = 1, set = 1) uniform sampler2D u_MetallicRoughnessTexture;
layout(binding = 2, set = 1) uniform sampler2D u_AOTexture;
layout(binding = 3, set = 1) uniform sampler2D u_EmissiveTexture;
layout(binding = 4, set = 1) uniform sampler2D u_NormalTexture;

layout(binding = 0, set = 2) uniform sampler2D u_ShadowMap;

const float PI = 3.14159265359;

vec3 DistributionGGX(vec3 N, vec3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0) + 1.0;
    return (a2 / (PI * denom * denom)) * vec3(1.0);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

vec3 FresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

float SampleShadow(vec3 worldPos, vec3 N, vec3 L)
{
    vec4 lightClip = u_Camera.u_ShadowViewProjection * vec4(worldPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    // 3x3 PCF for a soft shadow edge.
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(u_ShadowMap, 0));
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float d = texture(u_ShadowMap, proj.xy + vec2(float(x), float(y)) * texel).r;
            shadow += (proj.z - u_Camera.u_SSAOParams.z > d) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

void main()
{
    vec3 albedo = u_Object.u_Albedo.rgb;
    float metallic = u_Object.u_MetallicRoughness.x;
    float roughness = u_Object.u_MetallicRoughness.y;
    float ao = u_Object.u_MetallicRoughness.z;

    albedo *= texture(u_AlbedoTexture, vUV).rgb;
    vec3 mr = texture(u_MetallicRoughnessTexture, vUV).rgb;
    metallic *= mr.r;
    roughness *= mr.g;
    ao *= texture(u_AOTexture, vUV).r;
    vec3 emissive = u_Object.u_Emissive.rgb * texture(u_EmissiveTexture, vUV).rgb;

    vec3 N = normalize(vNormal);
    vec3 V = normalize(u_Camera.u_CameraPosition.xyz - vWorldPosition);
    vec3 L = normalize(u_Camera.u_LightDirection.xyz);
    vec3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);

    vec3 specular = (NDF * G * F) / max(vec3(4.0 * NdotV * NdotL), vec3(0.0001));

    vec3 kS = F;
    vec3 kD = (1.0 - kS) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    float shadow = SampleShadow(vWorldPosition, N, L);

    vec3 radiance = (diffuse + specular) * u_Camera.u_LightColor.rgb * u_Camera.u_LightColor.a;
    vec3 ambient = u_Camera.u_AmbientLight.rgb * albedo * ao;
    vec3 color = ambient + radiance * NdotL * shadow;
    color += emissive * u_Object.u_Emissive.a;

    oColor = vec4(color, 1.0);
}