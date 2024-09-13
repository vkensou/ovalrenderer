#include "descriptorsetpool.h"

namespace HGEGraphics
{
	DescriptorSetPool::DescriptorSetPool(CGPUDeviceId device, std::pmr::memory_resource* const memory_resource)
		: ResourcePool(10, nullptr, memory_resource), device(device), allocator(memory_resource)
	{
	}
	DescriptorSet* DescriptorSetPool::getDescriptorSet(const CGPUDescriptorSetDescriptor& descriptor)
	{
		return getResource(descriptor);
	}
	DescriptorSet* DescriptorSetPool::getResource_impl(const CGPUDescriptorSetDescriptor& descriptor)
	{
		auto handle = cgpu_create_descriptor_set(device, &descriptor);
		auto descriptorSet = allocator.new_object<DescriptorSet>();
		descriptorSet->_descriptor = descriptor;
		descriptorSet->handle = handle;
		return descriptorSet;
	}
	void DescriptorSetPool::destroyResource_impl(DescriptorSet* resource)
	{
		cgpu_free_descriptor_set(resource->handle);
		allocator.delete_object(resource);
	}
}