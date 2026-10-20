#version 450
#extension GL_EXT_scalar_block_layout : require

// The locations must match pbr.vert's outputs exactly: location 0 is
// vNormal, location 1 is vUV, location 2 is vWorldPosition. A varying
// that arrives at the wrong location is not a compile error - the
// pipeline is created and the fragment shader reads another varying's
// values, which is a rendering bug that looks like bad lighting.
layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPosition;

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

    // The scene's point lights: the lamps that colour a scene from inside
    // it. The count is fixed because the lights travel in this buffer, so
    // the engine picks the ones that make the show and tells the shader how
    // many there are.
    vec4 u_PointLightPosition[8];    // xyz = world position, w = unused
    vec4 u_PointLightColor[8];       // rgb = colour, a = intensity
    vec4 u_PointLightAttenuation[8]; // x = constant, y = linear, z = quadratic
    vec4 u_PointLightParams;         // x = how many lights there are
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

// Set 0, binding 2: material albedo map.
// The sampler is a separate descriptor (binding 3) rather than a combined
// image sampler: nvrhi's Vulkan backend has no
// VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER path, so a GLSL `sampler2D`
// could never be bound.
layout(binding = 2) uniform texture2D u_AlbedoTexture;

// Set 0, binding 3: sampler paired with the albedo map.
layout(binding = 3) uniform sampler u_AlbedoSampler;

// Set 0, binding 4: shadow map.
layout(binding = 4) uniform texture2D u_ShadowMap;

// Set 0, binding 5: sampler paired with the shadow map (comparison-free PCF).
layout(binding = 5) uniform sampler u_ShadowSampler;

// Set 0, binding 6: linear sampler kept for compatibility with other shaders.
layout(binding = 6) uniform sampler u_LinearSampler;

// Shadow map dimensions and bias (used for PCF and for fitting a surface to
// the map it is sampling against).
layout(binding = 7) uniform ShadowMapParams
{
    vec2 u_ShadowMapSize;
    vec2 u_ShadowBias;   // x = normal offset (world units), y = depth bias (NDC)
} u_Shadow;

// Set 0, binding 8: environment map (cube map for IBL).
layout(binding = 8) uniform textureCube u_EnvMap;

// Set 0, binding 9: sampler paired with the environment map.
layout(binding = 9) uniform sampler u_EnvSampler;

// Set 0, binding 10: irradiance map (cube map for diffuse IBL).
layout(binding = 10) uniform textureCube u_IrradianceMap;

// Set 0, binding 11: sampler paired with the irradiance map.
layout(binding = 11) uniform sampler u_IrradianceSampler;

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
float SampleShadow(vec3 worldPosition, vec3 normal, float ndotl)
{
    // Acne: a surface sampled against a map built from itself differs from its
    // own texel by a fraction of a texel, and the sign of that fraction is the
    // surface's slope against the light. A constant bias that is large enough
    // to cover a grazing surface detaches every shadow from the object that
    // cast it, so the offset scales with the slope and with the texel's own
    // world size - both of which the camera knows and the shader cannot guess.
    float slope = clamp((1.0 - clamp(ndotl, 0.0, 1.0)) / max(ndotl, 0.05), 0.0, 8.0);

    vec3 offset = normalize(normal) * (u_Shadow.u_ShadowBias.x * slope);

    vec4 lightClip = u_Camera.u_ShadowViewProjection * vec4(worldPosition + offset, 1.0);

    if (lightClip.w <= 0.0)
        return 1.0;

    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;

    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;



    float bias = u_Shadow.u_ShadowBias.y * (1.0 + slope);

    vec2 texel = 1.0 / u_Shadow.u_ShadowMapSize;
    float shadow = 0.0;
    for (int x = -1; x <= 1; x++)
    {
        for (int y = -1; y <= 1; y++)
        {
            float depth = texture(sampler2D(u_ShadowMap, u_ShadowSampler), proj.xy + vec2(float(x), float(y)) * texel).r;
            shadow += (proj.z - bias > depth) ? 0.0 : 1.0;
        }
    }
    return shadow / 9.0;
}

// IBL: Sample the environment map for specular reflections.
//
// The engine's cube maps carry a single mip level, so LOD 0 is the only level
// that exists: asking for 1..6 samples texels that are not there, which Vulkan
// leaves undefined and which shows up as a patch of someone else's texture.
// The roughness response that a prefiltered chain would have given is instead
// expressed as the blend toward the diffuse limit: a rough surface's reflection
// approaches its irradiance.
vec3 SampleEnvSpecular(vec3 R, float roughness, vec3 irradianceAtR)
{
    vec3 sharp = texture(samplerCube(u_EnvMap, u_EnvSampler), R).rgb;
    return mix(sharp, irradianceAtR, roughness);
}

// A point light's contribution to a surface.
//
// A lamp is not the sun: it falls off with distance, and it lights one
// surface at a time. The falloff is the classic point-light attenuation,
// 1 / (constant + linear * d + quadratic * d^2), and the light itself
// carries those three numbers.
//
// The diffuse term is Lambert and the highlight is a cheap Blinn-Phong: a
// lamp on a shiny block has to glint, or a light show is a wash.
vec3 PointLightContribution(int index, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness)
{
    vec4 position = u_Camera.u_PointLightPosition[index];
    vec4 color = u_Camera.u_PointLightColor[index];
    vec4 attenuation = u_Camera.u_PointLightAttenuation[index];

    vec3 toLight = position.xyz - vWorldPosition;
    float distance = length(toLight);
    if (distance < 0.0001)
        return vec3(0.0);

    vec3 L = toLight / distance;
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0)
        return vec3(0.0);

    float denominator = attenuation.x + attenuation.y * distance + attenuation.z * distance * distance;
    float falloff = 1.0 / max(denominator, 0.0001);

    vec3 H = normalize(V + L);
    float gloss = mix(64.0, 4.0, roughness);
    float specular = pow(max(dot(N, H), 0.0), gloss) * mix(0.04, 1.0, metallic);

    vec3 diffuse = (1.0 - metallic) * albedo / PI;

    return (diffuse + specular) * color.rgb * color.a * falloff * NdotL;
}

void main()
{
    vec3 albedo = u_Object.u_Albedo.rgb * texture(sampler2D(u_AlbedoTexture, u_AlbedoSampler), vUV).rgb;
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

    float shadow = SampleShadow(vWorldPosition, vNormal, NdotL);

    // IBL specular contribution.
    vec3 R = reflect(-V, N);
    vec3 irradianceAtR = texture(samplerCube(u_IrradianceMap, u_IrradianceSampler), R).rgb;
    vec3 envSpecular = SampleEnvSpecular(R, roughness, irradianceAtR);
    vec3 envSpecularContrib = envSpecular * F;

    vec3 radiance = (diffuse + specular) * u_Camera.u_LightColor.rgb * u_Camera.u_LightColor.a;

    // Diffuse IBL: sample irradiance map for ambient diffuse lighting.
    vec3 irradiance = texture(samplerCube(u_IrradianceMap, u_IrradianceSampler), N).rgb;
    vec3 diffuseIBL = irradiance * albedo * ao;

    vec3 ambient = u_Camera.u_AmbientLight.rgb * albedo * ao;

    // The lamps: each one adds its own falloff-weighted light, and the
    // count says how many there are - the rest of the array is not read.
    vec3 pointRadiance = vec3(0.0);
    int pointLightCount = int(u_Camera.u_PointLightParams.x);
    for (int i = 0; i < pointLightCount; ++i)
    {
        pointRadiance += PointLightContribution(i, N, V, albedo, metallic, roughness);
    }

    vec3 color = ambient + radiance * NdotL * shadow + envSpecularContrib + diffuseIBL + emissive + pointRadiance;

    oColor = vec4(color, 1.0);
}
