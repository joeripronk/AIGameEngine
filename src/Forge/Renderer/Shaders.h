#pragma once

#include <string>

namespace Forge::Shaders {

	// PBR Mesh Shader
	inline const char* PBRVertexShader = R"(
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

	uniform mat4 u_ViewProjection;
	uniform mat4 u_Transform;

	void main()
	{
		vPosition = aPosition;
		vNormal = aNormal;
		vUV = aUV;
		vTangent = aTangent;
		vBitangent = aBitangent;
		vWorldPosition = (u_Transform * vec4(aPosition, 1.0)).xyz;
		gl_Position = u_ViewProjection * u_Transform * vec4(aPosition, 1.0);
	}
	)";

	inline const char* PBRFragmentShader = R"(
	#version 450
	#extension GL_EXT_scalar_block_layout : require

	layout(location = 0) in vec3 vPosition;
	layout(location = 1) in vec3 vNormal;
	layout(location = 2) in vec2 vUV;
	layout(location = 3) in vec3 vTangent;
	layout(location = 4) in vec3 vBitangent;
	layout(location = 5) in vec3 vWorldPosition;

	layout(location = 0) out vec4 oColor;

	uniform vec3 u_Albedo;
	uniform float u_Metallic;
	uniform float u_Roughness;
	uniform float u_AO;
	uniform vec3 u_Emissive;
	uniform float u_EmissiveIntensity;
	uniform mat4 u_ViewProjection;

	uniform sampler2D u_AlbedoTexture;
	uniform sampler2D u_MetallicRoughnessTexture;
	uniform sampler2D u_AOTexture;
	uniform sampler2D u_EmissiveTexture;
	uniform sampler2D u_NormalTexture;

	uniform mat3 u_NormalMatrix;

	// IBL
	uniform samplerCube u_IBLTexture;
	uniform samplerCube u_BRDFLUT;
	uniform float u_IBLIntensity;

	// Lighting
	uniform vec3 u_LightColor;
	uniform vec3 u_LightDirection;
	uniform float u_LightIntensity;
	uniform vec3 u_AmbientLight;

	// Shadow
	uniform sampler2D u_ShadowMap;
	uniform mat4 u_ShadowViewProjection;

	const float PI = 3.14159265359;

	vec3 CalcNormal(vec3 normal, vec3 tangent, vec3 bitangent)
	{
		mat3 TBN = mat3(tangent, bitangent, normal);
		return normalize(TBN * normal);
	}

	float DistributionGGX(vec3 N, vec3 H, float roughness)
	{
		float a = roughness * roughness;
		float a2 = a * a;
		float NdotH = max(dot(N, H), 0.0);
		float NdotH2 = NdotH * NdotH;

		float num = a2;
		float denom = (NdotH2 * (a2 - 1.0) + 1.0);
		denom = PI * denom * denom;

		return num / denom;
	}

	float GeometrySchlickGGX(float NdotV, float roughness)
	{
		float r = roughness + 1.0;
		float k = (r * r) / 8.0;

		float num = NdotV;
		float denom = NdotV * (1.0 - k) + k;

		return num / denom;
	}

	float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness)
	{
		float NdotV = max(dot(N, V), 0.0);
		float NdotL = max(dot(N, L), 0.0);
		float ggx2 = GeometrySchlickGGX(NdotV, roughness);
		float ggx1 = GeometrySchlickGGX(NdotL, roughness);

		return ggx1 * ggx2;
	}

	vec3 FresnelSchlick(float cosTheta, vec3 F0)
	{
		return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
	}

	vec3 FresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness)
	{
		return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
	}

	void main()
	{
		vec3 albedo = u_Albedo;
		float metallic = u_Metallic;
		float roughness = u_Roughness;
		float ao = u_AO;
		vec3 emissive = u_Emissive;
		float emissiveIntensity = u_EmissiveIntensity;

		// Sample textures
		vec3 albedoTex = texture(u_AlbedoTexture, vUV).rgb;
		vec3 metallicRoughnessTex = texture(u_MetallicRoughnessTexture, vUV).rgb;
		vec3 aoTex = texture(u_AOTexture, vUV).rgb;
		vec3 emissiveTex = texture(u_EmissiveTexture, vUV).rgb;
		vec3 normalTex = texture(u_NormalTexture, vUV).rgb;

		albedo = albedo * albedoTex;
		metallic = metallic * metallicRoughnessTex.r;
		roughness = roughness * metallicRoughnessTex.g;
		ao = ao * aoTex.r;
		emissive = emissive * emissiveTex;
		normal = normalize(normalTex * 2.0 - 1.0);

		vec3 N = normalize(normal);
		vec3 V = normalize(cameraPosition - vWorldPosition);

		// Lighting
		vec3 L = normalize(u_LightDirection);
		vec3 H = normalize(V + L);

		float NdotL = max(dot(N, L), 0.0);
		float NdotV = max(dot(N, V), 0.0);

		// BRDF
		vec3 F0 = mix(vec3(0.04), albedo, metallic);
		F0 = FresnelSchlick(NdotV, F0);

		float NDF = DistributionGGX(N, H, roughness);
		float G = GeometrySmith(N, V, L, roughness);
		vec3 F = FresnelSchlick(max(dot(H, V), 0.0), F0);

		vec3 numerator = NDF * G * F;
		float denominator = 4.0 * max(dot(N, V), 0.0) * NdotL + 0.0001;
		vec3 specular = numerator / denominator;

		// Energy conservation
		vec3 kS = F;
		vec3 kD = (1.0 - kS) * (1.0 - metallic);

		vec3 diffuse = kD * albedo / PI;

		// IBL
		vec3 R = reflect(-V, N);
		vec3 irradiance = texture(u_IBLTexture, R).rgb * u_IBLIntensity;
		vec3 brdf = texture(u_BRDFLUT, vec2(NdotV, roughness)).rgb;
		vec3 indirectSpecular = irradiance * brdf;

		// Shadow
		float shadow = 1.0;
		{
			vec4 shadowProj = u_ShadowViewProjection * vec4(vWorldPosition, 1.0);
			vec3 shadowCoords = shadowProj.xyz / shadowProj.w;
			if (shadowCoords.z > 0.0 && shadowCoords.z < 1.0)
			{
				float shadowDepth = texture(u_ShadowMap, shadowCoords.xy).r;
				shadow = (shadowCoords.z >= shadowDepth + 0.005) ? 1.0 : 0.0;
			}
		}

		// Final
		vec3 ambient = (kD * albedo * ao + indirectSpecular) * u_AmbientLight;
		vec3 radiance = (diffuse + specular) * NdotL * u_LightIntensity * u_LightColor + emissive * emissiveIntensity;
		vec3 color = (ambient + radiance * shadow) * albedo;

		oColor = vec4(color, 1.0);
	}
	)";

	// SSAO Shader
	inline const char* SSAOVertexShader = R"(
	#version 450

	layout(location = 0) in vec2 aPosition;
	layout(location = 1) in vec3 aNormal;
	layout(location = 2) in vec3 aViewPos;

	layout(location = 0) out vec3 vNormal;
	layout(location = 1) out vec3 vViewPos;

	uniform mat4 u_ViewProjection;

	void main()
	{
		vNormal = aNormal;
		vViewPos = aViewPos;
		gl_Position = vec4(aPosition, 0.0, 1.0);
	}
	)";

	inline const char* SSAOFragmentShader = R"(
	#version 450

	layout(location = 0) in vec3 vNormal;
	layout(location = 1) in vec3 vViewPos;

	layout(location = 0) out float oSSAO;

	uniform sampler2D u_GBuffer0;
	uniform sampler2D u_GBuffer1;
	uniform float u_SSAOIntensity;

	uniform vec3 samplePosArray[32];

	const int SAMPLE_COUNT = 32;
	const float SAMPLE_RADIUS = 0.5;

	// Hash function for sample position generation
	int Hash(int x)
	{
		x = 2097151 * (x & 0x7FFF) + (x & 0x7FFF);
		x = (x + (x >> 13)) & 0x7FFFFFFF;
		return x;
	}

	vec3 GetSamplePos(int index)
	{
		int i = Hash(index);
		return vec3(
			float(i % 256) / 128.0 - 1.0,
			float((i / 256) % 256) / 128.0 - 1.0,
			float((i / 65536) % 256) / 128.0 - 1.0
		);
	}

	void main()
	{
		vec3 normal = texture(u_GBuffer1, gl_FragCoord.xy / vec2(textureSize(u_GBuffer1, 0))).rgb;
		vec3 viewPos = texture(u_GBuffer0, gl_FragCoord.xy / vec2(textureSize(u_GBuffer0, 0))).rgb;

		vec3 randomVec = normalize(vec3(
			sin(gl_FragCoord.x) * cos(gl_FragCoord.y),
			cos(gl_FragCoord.x) * sin(gl_FragCoord.y),
			sin(gl_FragCoord.x) * sin(gl_FragCoord.y)
		));

		mat3 TBN = mat3(
			normalize(normal - vec3(0.1)) - randomVec,
			randomVec,
			normalize(cross(normal, randomVec))
		);

		float occlusion = 0.0;
		for (int i = 0; i < SAMPLE_COUNT; i++)
		{
			vec3 samplePos = GetSamplePos(i);
			samplePos = TBN * samplePos * SAMPLE_RADIUS;
			vec3 sampleViewPos = viewPos + samplePos;
			float sampleDepth = length(sampleViewPos - viewPos);
			float rangeCheck = smoothstep(0.0, 1.0, SAMPLE_RADIUS / abs(sampleDepth - viewPos.z));
			occlusion += (sampleDepth >= sampleViewPos.z ? 1.0 : 0.0) * rangeCheck;
		}

		occlusion = 1.0 - (occlusion / SAMPLE_COUNT) * u_SSAOIntensity;
		oSSAO = occlusion;
	}
	)";

	// HDR Tonemapping Shader
	inline const char* HDRVertexShader = R"(
	#version 450

	layout(location = 0) in vec2 aPosition;
	layout(location = 1) in vec2 aUV;

	layout(location = 0) out vec2 vUV;

	void main()
	{
		vUV = aUV;
		gl_Position = vec4(aPosition, 0.0, 1.0);
	}
	)";

	inline const char* HDRFragmentShader = R"(
	#version 450

	layout(location = 0) in vec2 vUV;
	layout(location = 0) out vec4 oColor;

	uniform sampler2D u_HDRBuffer;
	uniform float u_Exposure;

	const float gamma = 2.2;

	void main()
	{
		vec3 hdrColor = texture(u_HDRBuffer, vUV).rgb;

		// Tonemapping (ACES approximation)
		vec3 tonemapped = hdrColor / (hdrColor + vec3(1.0));
		tonemapped = pow(tonemapped, vec3(1.0 / gamma));

		oColor = vec4(tonemapped, 1.0);
	}
	)";

	// Final Composite Shader
	inline const char* FinalVertexShader = R"(
	#version 450

	layout(location = 0) in vec2 aPosition;
	layout(location = 1) in vec2 aUV;

	layout(location = 0) out vec2 vUV;

	void main()
	{
		vUV = aUV;
		gl_Position = vec4(aPosition, 0.0, 1.0);
	}
	)";

	inline const char* FinalFragmentShader = R"(
	#version 450

	layout(location = 0) in vec2 vUV;
	layout(location = 0) out vec4 oColor;

	uniform sampler2D u_TonemappedBuffer;
	uniform sampler2D u_SSAOBuffer;
	uniform float u_SSAOIntensity;

	void main()
	{
		vec3 color = texture(u_TonemappedBuffer, vUV).rgb;
		float ssao = texture(u_SSAOBuffer, vUV).r;

		color *= mix(1.0, ssao, u_SSAOIntensity);
		oColor = vec4(color, 1.0);
	}
	)";

}
