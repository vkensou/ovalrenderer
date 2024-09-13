#include "framebufferpool.h"

namespace HGEGraphics
{
	FramebufferPool::FramebufferPool(CGPUDeviceId device, std::pmr::memory_resource* const memory_resource)
		: ResourcePool(10, nullptr, memory_resource), device(device), allocator(memory_resource)
	{
	}
	Framebuffer* FramebufferPool::getFramebuffer(const CGPUFramebufferDescriptor& descriptor)
	{
		return getResource(descriptor);
	}

	Framebuffer* FramebufferPool::getResource_impl(const CGPUFramebufferDescriptor& key)
	{
		auto cgpuFramebuffer = cgpu_create_framebuffer(device, &key);

		auto renderPass = allocator.new_object<Framebuffer>();
		renderPass->framebuffer = cgpuFramebuffer;
		renderPass->_descriptor = key;
		return renderPass;
	}

	void FramebufferPool::destroyResource_impl(Framebuffer* resource)
	{
		cgpu_free_framebuffer(resource->framebuffer);
		resource->framebuffer = CGPU_NULLPTR;
		resource->_descriptor = {};
		allocator.delete_object(resource);
	}
}