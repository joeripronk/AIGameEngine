#version 450
#extension GL_EXT_scalar_block_layout : require

// The text's vertex shader.
//
// The text is a mesh of the glyphs' quads, and the atlas says how much of
// the colour each one covers. The pipeline blends it, so the text is drawn
// over the scene rather than into it.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vColor;

// Set 0, binding 0: per-frame data. The block is the same one the scene's
// shader uses, so the two can share a descriptor set.
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

// Set 0, binding 1: per-object data. The transform places the text and the
// albedo is its colour; the members the text does not use are still here,
// because the block's layout has to match the one the scene's shader uses.
layout(binding = 1) uniform ObjectData
{
    mat4 u_Transform;
    mat4 u_NormalMatrix;
    vec4 u_Albedo;
    vec4 u_MetallicRoughnessAO;
    vec4 u_Emissive;
} u_Object;

void main()
{
    vec4 worldPosition = u_Object.u_Transform * vec4(aPosition, 1.0);
    gl_Position = u_Camera.u_ViewProjection * worldPosition;

    vUV = aUV;
    vColor = u_Object.u_Albedo.rgb;
}
