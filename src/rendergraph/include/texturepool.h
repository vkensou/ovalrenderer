#pragma once

#include "resourcepool.h"
#include "cgpu/api.h"
#include "hash.h"

namespace HGEGraphics
{
	struct TextureDescriptor
	{
		uint16_t width = 0;
		uint16_t height = 0;
		uint16_t depth = 0;
		uint16_t mipLevels = 0;
		ECGPUFormat format = CGPU_FORMAT_UNDEFINED;

		bool operator==(const TextureDescriptor& other) const;
	};

	struct TextureDescriptorHasher
	{
		size_t operator()(const TextureDescriptor& xyz) const {
			return MurmurHashFn<TextureDescriptor>()(xyz);
		}
	};

	struct Texture;
	struct TextureWrap
	{
		TextureDescriptor descriptor() const
		{
			return _descriptor;
		}
		TextureDescriptor _descriptor;
		Texture* texture;
	};

	class TexturePool
		: public ResourcePool<TextureDescriptor, TextureWrap, false, true, TextureDescriptorHasher>
	{
	public:
		TexturePool(TexturePool* upstream, std::pmr::memory_resource* const memory_resource);

		TextureWrap* getTexture(uint16_t width, uint16_t height, ECGPUFormat format);
	};

	class CgpuTexturePool
		: public TexturePool
	{
	public:
		CgpuTexturePool(CGPUDeviceId device, CGPUQueueId gfx_queue, TexturePool* upstream, std::pmr::memory_resource* const memory_resource);

	protected:
		TextureWrap* getResource_impl(const TextureDescriptor& descriptor) override;
		void destroyResource_impl(TextureWrap* resource) override;

	private:
		CGPUDeviceId device;
		CGPUQueueId gfx_queue;
		std::pmr::polymorphic_allocator<> allocator;
	};
}
