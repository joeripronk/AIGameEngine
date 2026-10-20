#include "fgpch.h"
#include "Platform/Vulkan/Pipeline/VertexLayout.h"

#include "Forge/Core/Log.h"

namespace Forge { namespace Vulkan {

VertexLayoutError VertexLayout::Validate(const VertexFieldList& fields, uint32_t structSize)
{
	if (fields.Count == 0 || fields.Count > fields.Fields.size())
		return VertexLayoutError::Empty;

	uint32_t running = 0;
	for (uint32_t i = 0; i < fields.Count; ++i)
	{
		const VertexField& field = fields.Fields[i];
		if (field.Format == VK_FORMAT_UNDEFINED)
			return VertexLayoutError::FormatUnsupported;
		if (field.Size == 0)
			return VertexLayoutError::FieldSizeMismatch;
		if (field.Offset != running)
			return VertexLayoutError::FieldSizeMismatch;
		running += field.Size;
	}

	if (running != structSize)
		return VertexLayoutError::FieldSizeMismatch;

	return VertexLayoutError::Ok;
}

VertexLayout VertexLayout::FromFields(const VertexFieldList& fields, uint32_t structSize, uint32_t binding, VkVertexInputRate inputRate, uint32_t firstLocation)
{
	VertexLayout layout{};

	if (const VertexLayoutError error = Validate(fields, structSize); error != VertexLayoutError::Ok)
	{
		FG_CORE_ERROR("VertexLayout: field list invalid (error {0}); layout left empty", (int)error);
		return layout;
	}

	layout.Stride = structSize;
	layout.Binding = VkVertexInputBindingDescription{ binding, structSize, inputRate };

	for (uint32_t i = 0; i < fields.Count; ++i)
	{
		layout.Attributes[i] = VkVertexInputAttributeDescription{ firstLocation + i, binding, fields.Fields[i].Format, fields.Fields[i].Offset };
	}
	layout.AttributeCount = fields.Count;

	return layout;
}

}}
