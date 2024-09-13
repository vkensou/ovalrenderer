#pragma once

#include "resourcepool.h"
#include "cgpu/api.h"
#include "hash.h"
#include <string.h>

namespace HGEGraphics
{
	struct TextureViewDescriptorHasher
	{
		inline size_t operator()(const CGPUTextureViewDescriptor& a) const
		{
			return MurmurHashFn<CGPUTextureViewDescriptor>()(a);
		}
	};

	struct TextureViewDescriptorEq
	{
		inline bool operator()(const CGPUTextureViewDescriptor& a, const CGPUTextureViewDescriptor& b) const
		{
			return !(bool)memcmp(&a, &b, sizeof(CGPUTextureViewDescriptor));
		}
	};

	struct TextureView
	{
		CGPUTextureViewDescriptor descriptor() const
		{
			return _descriptor;
		}
		CGPUTextureViewId handle;
		CGPUTextureViewDescriptor _descriptor;
	};

	class TextureViewPool
		: public ResourcePool<CGPUTextureViewDescriptor, TextureView, true, true, TextureViewDescriptorHasher, TextureViewDescriptorEq>
	{
	public:
		TextureViewPool(TextureViewPool* upstream, std::pmr::memory_resource* const memory_resource);

	protected:
		// Í¨¹ý ResourcePool ¼Ì³Ð
		virtual TextureView* getResource_impl(const CGPUTextureViewDescriptor& descriptor) override;

		virtual void destroyResource_impl(TextureView* resource) override;

	private:
		CGPUDeviceId device{ CGPU_NULLPTR };
		std::pmr::polymorphic_allocator<> allocator;
	};
}