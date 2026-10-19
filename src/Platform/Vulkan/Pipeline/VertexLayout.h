#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace Forge { namespace Vulkan {

// Vertex layout from the CPU struct (BRUTAL VertexInput.cs): the C++ struct
// *is* the GPU layout. FG_VERTEX_FIELDS declares the fields in declaration
// order; the template maps member types to VkFormats and computes offsets
// from the real struct - a hand-maintained attribute array that drifts from
// the struct is exactly what this replaces.

enum class VertexLayoutError : uint8_t { Ok, Empty, FieldSizeMismatch, FormatUnsupported };

// Declare the vertex fields once, in struct order. The list drives offsets,
// formats and the stride; the struct itself is never re-read at runtime.
#define FG_VERTEX_FIELDS(STRUCT, ...) \
	static constexpr ::Forge::Vulkan::VertexFieldList FieldList() { return ::Forge::Vulkan::MakeFieldList(__VA_ARGS__); }

struct VertexField
{
	uint32_t Offset = 0;
	uint32_t Size = 0;
	VkFormat Format = VK_FORMAT_UNDEFINED;
};

struct VertexFieldList
{
	std::array<VertexField, 16> Fields{};
	uint32_t Count = 0;
};

// Field type tags: each maps to the shader-compatible format(s) BRUTAL's
// FormatMap assigns. Normalized byte4 is the Normalize behaviour.
struct FieldFloat2 { using Type = float; static constexpr uint32_t Components = 2; };
struct FieldFloat3 { using Type = float; static constexpr uint32_t Components = 3; };
struct FieldFloat4 { using Type = float; static constexpr uint32_t Components = 4; };
struct FieldUint32 { using Type = uint32_t; static constexpr uint32_t Components = 1; };
struct FieldInt32 { using Type = int32_t; static constexpr uint32_t Components = 1; };
struct FieldByte4Normalized { using Type = uint8_t; static constexpr uint32_t Components = 4; };

namespace FieldDetail {

	constexpr VkFormat FloatFormat(uint32_t components)
	{
		switch (components)
		{
		case 1: return VK_FORMAT_R32_SFLOAT;
		case 2: return VK_FORMAT_R32G32_SFLOAT;
		case 3: return VK_FORMAT_R32G32B32_SFLOAT;
		case 4: return VK_FORMAT_R32G32B32A32_SFLOAT;
		default: return VK_FORMAT_UNDEFINED;
		}
	}

}

// Field constructors: the offset is the running byte position the caller
// threads through; each returns the field plus the advance.
constexpr VertexField MakeField(size_t offset, FieldFloat2) { return VertexField{ static_cast<uint32_t>(offset), 8, FieldDetail::FloatFormat(2) }; }
constexpr VertexField MakeField(size_t offset, FieldFloat3) { return VertexField{ static_cast<uint32_t>(offset), 12, FieldDetail::FloatFormat(3) }; }
constexpr VertexField MakeField(size_t offset, FieldFloat4) { return VertexField{ static_cast<uint32_t>(offset), 16, FieldDetail::FloatFormat(4) }; }
constexpr VertexField MakeField(size_t offset, FieldUint32) { return VertexField{ static_cast<uint32_t>(offset), 4, VK_FORMAT_R32_UINT }; }
constexpr VertexField MakeField(size_t offset, FieldInt32) { return VertexField{ static_cast<uint32_t>(offset), 4, VK_FORMAT_R32_SINT }; }
constexpr VertexField MakeField(size_t offset, FieldByte4Normalized) { return VertexField{ static_cast<uint32_t>(offset), 4, VK_FORMAT_R8G8B8A8_UNORM }; }

// Variadic field list: fields are listed in struct order; offsets accumulate
// from the sizes, and every member's real sizeof must match the declared
// size (the FieldSizeMismatch validation).
template <typename... Tags>
constexpr VertexFieldList MakeFieldList(Tags... tags)
{
	const VertexField declared[] = { MakeField(0, tags)... };
	VertexFieldList list{};
	size_t running = 0;
	for (const VertexField& field : declared)
	{
		list.Fields[list.Count] = field;
		list.Fields[list.Count].Offset = static_cast<uint32_t>(running);
		running += field.Size;
		++list.Count;
	}
	return list;
}

struct VertexLayout final
{
	VkVertexInputBindingDescription Binding{};
	std::array<VkVertexInputAttributeDescription, 16> Attributes{};
	uint32_t AttributeCount = 0;
	uint32_t Stride = 0;

	// Build from the declared field list; validates that the field sizes
	// tile the struct exactly - no gaps, no overrun.
	[[nodiscard]] static VertexLayout FromFields(const VertexFieldList& fields, uint32_t structSize, uint32_t binding = 0, VkVertexInputRate inputRate = VK_VERTEX_INPUT_RATE_VERTEX);

	// Gaps, overrun, empty list, unsupported format.
	[[nodiscard]] static VertexLayoutError Validate(const VertexFieldList& fields, uint32_t structSize);

	[[nodiscard]] std::span<const VkVertexInputBindingDescription> BindingSpan() const { return std::span<const VkVertexInputBindingDescription>(&Binding, 1); }
	[[nodiscard]] std::span<const VkVertexInputAttributeDescription> AttributeSpan() const { return std::span<const VkVertexInputAttributeDescription>(Attributes.data(), AttributeCount); }
};

}}
