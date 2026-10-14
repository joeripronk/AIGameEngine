#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

layout(location = 0) out vec3 vPosition;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec3 vTangent;
layout(location = 4) out vec3 vBitangent;
layout(location = 5) out vec3 vWorldPosition;

layout(binding = 0) uniform CameraData
{
    mat4 u_ViewProjection;
    mat4 u_ShadowViewProjection;
    vec4 u_LightDirection;   // xyz dir, w unused
    vec4 u_LightColor;       // rgb color, a intensity
    vec4 u_CameraPosition;   // xyz pos, w unused
    vec4 u_AmbientLight;     // rgb ambient, a unused
    vec4 u_SSAOParams;       // intensity, radius, bias, power
} u_Camera;

layout(binding = 1, set = 0) uniform ObjectData
{
    mat4 u_Transform;
    vec4 u_Albedo;
    vec4 u_MetallicRoughness;  // x metallic, y roughness, z AO, w unused
    vec4 u_Emissive;          // rgb emissive, a intensity
    mat4 u_NormalMatrix;
} u_Object;

layout(binding = 0, set = 1) uniform sampler2D u_AlbedoTexture;
layout(binding = 1, set = 1) uniform sampler2D u_MetallicRoughnessTexture;
layout(binding = 2, set = 1) uniform sampler2D u_AOTexture;
layout(binding = 3, set = 1) uniform sampler2D u_EmissiveTexture;
layout(binding = 4, set = 1) uniform sampler2D u_NormalTexture;

void main()
{
    vPosition = aPosition;
    vNormal = mat3(u_Object.u_NormalMatrix) * aNormal;
    vUV = aUV;
    vTangent = mat3(u_Object.u_NormalMatrix) * aTangent;
    vBitangent = mat3(u_Object.u_NormalMatrix) * aBitangent;
    vec4 worldPos = u_Object.u_Transform * vec4(aPosition, 1.0);
    vWorldPosition = worldPos.xyz;
    gl_Position = u_Camera.u_ViewProjection * worldPos;
}