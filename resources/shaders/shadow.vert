#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vUV;

layout(binding = 0) uniform ShadowData
{
    mat4 u_ShadowViewProjection;
} u_Shadow;

layout(binding = 1, set = 0) uniform ObjectData
{
    mat4 u_Transform;
    vec4 u_Albedo;
    vec4 u_MetallicRoughness;
    vec4 u_Emissive;
    mat4 u_NormalMatrix;
} u_Object;

void main()
{
    vNormal = mat3(u_Object.u_NormalMatrix) * aNormal;
    vUV = aUV;
    vec4 worldPos = u_Object.u_Transform * vec4(aPosition, 1.0);
    gl_Position = u_Shadow.u_ShadowViewProjection * worldPos;
}