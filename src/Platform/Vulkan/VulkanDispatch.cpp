// nvrhi's Vulkan backend expects the Vulkan-Hpp dynamic dispatch storage
// (vk::detail::defaultDispatchLoaderDynamic) to be provided by a single
// translation unit via the VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE
// macro. Newer Vulkan-Hpp releases gate that definition behind the C++20 module
// path instead, so we provide the storage explicitly here to keep the Vulkan
// backend linkable.

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

namespace vk::detail {
	VULKAN_HPP_STORAGE_API DispatchLoaderDynamic defaultDispatchLoaderDynamic;
}