#version 450
#extension GL_EXT_scalar_block_layout : require

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

layout(location = 0) out vec2 vUV;

layout(binding = 1) uniform ObjectData
{
    mat4 u_Transform;
    mat4 u_NormalMatrix;
    vec4 u_Albedo;
    vec4 u_MetallicRoughnessAO;
    vec4 u_Emissive;
} u_Object;

layout(binding = 4) uniform ShadowData
{
    mat4 u_ShadowViewProjection;
} u_Shadow;

void main()
{
    vUV = aUV;
    vec4 worldPosition = u_Object.u_Transform * vec4(aPosition, 1.0);
    gl_Position = u_Shadow.u_ShadowViewProjection * worldPosition;
}
