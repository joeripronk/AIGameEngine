#version 450

// The shadow pass is depth only: its render pass has no colour
// attachment, so the fragment shader writes no colour output. Declaring
// one anyway is a write into nowhere - the validation layer reports it
// on every pipeline creation.
//
// The depth itself is the rasteriser's: gl_FragCoord.z is what the
// depth attachment receives.
//
// shadow.vert outputs vUV at location 0. The varying is unused here, but
// it has to be declared at the location the vertex stage writes it, or
// the two stages disagree about their interface.
layout(location = 0) in vec2 vUV;

void main()
{
}
