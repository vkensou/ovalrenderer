#include "bufferpool.h"

namespace HGEGraphics
{
	BufferPool::BufferPool(CGPUDeviceId device, BufferPool* upstream, std::pmr::memory_resource* const memory_resource)
		: ResourcePool(12, upstream, memory_resource), device(device), allocator(memory_resource)
	{
	}
	BufferWrap* BufferPool::getResource_impl(const CGPUBufferDescriptor& descriptor)
	{
		auto handle = cgpu_create_buffer(device, &descriptor);
		auto buffer = allocator.new_object<BufferWrap>();
		buffer->_descriptor = descriptor;
		buffer->handle = handle;
		buffer->cur_state = CGPU_RESOURCE_STATE_UNDEFINED;
		return buffer;
	}
	void BufferPool::destroyResource_impl(BufferWrap* resource)
	{
		cgpu_free_buffer(resource->handle);
		allocator.delete_object(resource);
	}
}