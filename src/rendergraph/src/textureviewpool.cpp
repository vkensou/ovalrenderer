#include "textureviewpool.h"

namespace HGEGraphics
{
	TextureViewPool::TextureViewPool(TextureViewPool* upstream, std::pmr::memory_resource* const memory_resource)
		: ResourcePool(11, upstream, memory_resource), allocator(memory_resource)
	{
	}
	TextureView* TextureViewPool::getResource_impl(const CGPUTextureViewDescriptor& descriptor)
	{
		auto handle = cgpu_create_texture_view(descriptor.texture->device, &descriptor);
		auto texture_view = allocator.new_object<TextureView>();
		texture_view->_descriptor = descriptor;
		texture_view->handle = handle;
		return texture_view;
	}
	void TextureViewPool::destroyResource_impl(TextureView* resource)
	{
		cgpu_free_texture_view(resource->handle);
		allocator.delete_object(resource);
	}
}