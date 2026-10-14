#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPosition;

// Set 0, binding 0: per-frame data.
layout(binding = 0) uniform CameraData
{
    mat4 u_ViewProjection;
    mat4 u_ShadowViewProjection;
    vec4 u_LightDirection;   // xyz = direction light travels, w unused
    vec4 u_LightColor;       // rgb = colour, a = intensity
    vec4 u_CameraPosition;   // xyz = world position
    vec4 u_AmbientLight;     // rgb = ambient colour
    vec4 u_SSAOParams;       // x = shadow bias, y = ao strength, zw unused
} u_Camera;

// Set 0, binding 1: per-object data.
layout(binding = 1) uniform ObjectData
{
    mat4 u_Transform;
    mat4 u_NormalMatrix;
    vec4 u_Albedo;               // rgb = base colour
    vec4 u_MetallicRoughnessAO;  // x = metallic, y = roughness, z = ao
    vec4 u_Emissive;             // rgb = emissive, a = intensity
} u_Object;

void main()
{
    mat3 normalMatrix = mat3(u_Object.u_NormalMatrix);

    vNormal = normalMatrix * aNormal;
    vUV = aUV;

    vec4 worldPosition = u_Object.u_Transform * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;

    gl_Position = u_Camera.u_ViewProjection * worldPosition;
}
