#include "texturepool.h"
#include "renderer.h"

namespace HGEGraphics
{
	bool HGEGraphics::TextureDescriptor::operator==(const TextureDescriptor& other) const
	{
		return width == other.width && height == other.height
			&& depth == other.depth && mipLevels == other.mipLevels
			&& format == other.format;
	}

	TexturePool::TexturePool(TexturePool* upstream, std::pmr::memory_resource* const memory_resource)
		: ResourcePool(12, upstream, memory_resource)
	{
	}

	TextureWrap* TexturePool::getTexture(uint16_t width, uint16_t height, ECGPUFormat format)
	{
		TextureDescriptor key = { width, height, 1, 1, format };
		return getResource(key);
	}
	CgpuTexturePool::CgpuTexturePool(CGPUDeviceId device, CGPUQueueId gfx_queue, TexturePool* upstream, std::pmr::memory_resource* const memory_resource)
		: TexturePool(upstream, memory_resource), device(device), gfx_queue(gfx_queue), allocator(memory_resource)
	{
	}
	TextureWrap* CgpuTexturePool::getResource_impl(const TextureDescriptor& descriptor)
	{
		bool isDepth =
			descriptor.format == CGPU_FORMAT_D32_SFLOAT_S8_UINT ||
			descriptor.format == CGPU_FORMAT_D24_UNORM_S8_UINT ||
			descriptor.format == CGPU_FORMAT_D16_UNORM_S8_UINT ||
			descriptor.format == CGPU_FORMAT_D32_SFLOAT ||
			descriptor.format == CGPU_FORMAT_X8_D24_UNORM ||
			descriptor.format == CGPU_FORMAT_D16_UNORM;
		CGPUTextureDescriptor texture_desc =
		{
			.flags = CGPU_TCF_FORCE_2D,
			.width = descriptor.width,
			.height = descriptor.height,
			.depth = 1,
			.array_size = 1,
			.format = descriptor.format,
			.mip_levels = 1,
			.owner_queue = gfx_queue,
			.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
			.descriptors = (CGPUResourceTypes)(CGPU_RESOURCE_TYPE_TEXTURE | (isDepth ? CGPU_RESOURCE_TYPE_DEPTH_STENCIL : CGPU_RESOURCE_TYPE_RENDER_TARGET)),
		};

		auto texture = cgpu_create_texture(device, &texture_desc);

		TextureWrap* resource = allocator.new_object<TextureWrap>();
		resource->_descriptor = descriptor;
		resource->texture = allocator.new_object<Texture>();
		resource->texture->handle = texture;
		resource->texture->view = nullptr;
		resource->texture->cur_states.resize(descriptor.depth * descriptor.mipLevels);
		std::fill(resource->texture->cur_states.begin(), resource->texture->cur_states.end(), CGPU_RESOURCE_STATE_UNDEFINED);
		resource->texture->states_consistent = true;
		return resource;
	}
	void CgpuTexturePool::destroyResource_impl(TextureWrap* resource)
	{
		cgpu_free_texture(resource->texture->handle);
		resource->texture->handle = CGPU_NULLPTR;
		resource->texture->cur_states.clear();
		allocator.delete_object(resource->texture);
		allocator.delete_object(resource);
	}
}
