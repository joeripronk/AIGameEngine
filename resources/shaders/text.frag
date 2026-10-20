#version 450

// The text's fragment shader.
//
// The atlas holds the glyphs in white with their coverage in the alpha, so
// the coverage is the alpha the pipeline blends with.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec3 vColor;

layout(location = 0) out vec4 oColor;

// Set 0, binding 2: the atlas of the glyphs.
layout(binding = 2) uniform texture2D u_Texture;

// Set 0, binding 3: sampler paired with the atlas.
layout(binding = 3) uniform sampler u_Sampler;

void main()
{
    float coverage = texture(sampler2D(u_Texture, u_Sampler), vUV).a;
    oColor = vec4(vColor, coverage);
}
