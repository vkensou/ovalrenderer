#pragma once

#include "cgpu/api.h"
#include "resourcepool.h"
#include "hash.h"

namespace HGEGraphics
{
	struct Framebuffer
	{
		CGPUFramebufferDescriptor descriptor() const
		{
			return _descriptor;
		}
		CGPUFramebufferId framebuffer;
		CGPUFramebufferDescriptor _descriptor;
	};

	struct FramebufferDescriptorHasher
	{
		inline size_t operator()(const CGPUFramebufferDescriptor& a) const
		{
			return MurmurHashFn<CGPUFramebufferDescriptor>()(a);
		}
	};

	struct FramebufferDescriptorEq
	{
		inline bool operator()(const CGPUFramebufferDescriptor& a, const CGPUFramebufferDescriptor& b) const
		{
			return !(bool)memcmp(&a, &b, sizeof(CGPUFramebufferDescriptor));
		}
	};

	class FramebufferPool
		: public ResourcePool<CGPUFramebufferDescriptor, Framebuffer, true, true, FramebufferDescriptorHasher, FramebufferDescriptorEq>
	{
	public:
		FramebufferPool(CGPUDeviceId device, std::pmr::memory_resource* const memory_resource);

		Framebuffer* getFramebuffer(const CGPUFramebufferDescriptor& descriptor);

	protected:
		// Í¨¹ý ResourcePool ¼Ì³Ð
		virtual Framebuffer* getResource_impl(const CGPUFramebufferDescriptor& descriptor) override;
		virtual void destroyResource_impl(Framebuffer* resource) override;

	private:
		CGPUDeviceId device{ CGPU_NULLPTR };
		std::pmr::polymorphic_allocator<> allocator;
	};
}