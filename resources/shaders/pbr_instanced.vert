#version 450
#extension GL_EXT_scalar_block_layout : require

// The instanced variant of pbr.vert: the model matrix arrives as vertex
// attributes (one per instance) instead of from the per-object uniform
// buffer, so one draw can put the same mesh in many places.
//
// The mesh's attributes are at locations 0 to 4, so the instance's rows are
// at 5 to 8. A location that arrives at the wrong one is not a compile
// error - the pipeline is created and the vertex shader reads another
// attribute's values, which is a rendering bug that looks like garbage.

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec3 aTangent;
layout(location = 4) in vec3 aBitangent;

layout(location = 5) in vec4 iModelRow0;
layout(location = 6) in vec4 iModelRow1;
layout(location = 7) in vec4 iModelRow2;
layout(location = 8) in vec4 iModelRow3;

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
    vec4 u_SSAOParams;       // x unused (the shadow bias lives in ShadowMapParams), y = ao strength, zw unused
} u_Camera;

// Set 0, binding 1: per-object data. The transform is the identity here -
// the model matrix is the instance's - and the rest is the material, which
// every instance of the draw shares.
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
    mat4 model = mat4(iModelRow0, iModelRow1, iModelRow2, iModelRow3);

    // The normal matrix is the inverse transpose of the model matrix. The
    // shader computes it because the instance data carries the model matrix
    // only: a second matrix per instance would be a second thing to keep in
    // step with the first.
    mat3 normalMatrix = mat3(transpose(inverse(model)));

    vNormal = normalMatrix * aNormal;
    vUV = aUV;

    vec4 worldPosition = model * vec4(aPosition, 1.0);
    vWorldPosition = worldPosition.xyz;

    gl_Position = u_Camera.u_ViewProjection * worldPosition;
}
