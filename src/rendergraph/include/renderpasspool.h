#pragma once

#include "cgpu/api.h"
#include "resourcepool.h"
#include "hash.h"

namespace HGEGraphics
{
	struct RenderPass
	{
		CGPURenderPassDescriptor descriptor() const
		{
			return _descriptor;
		}
		CGPURenderPassId renderPass;
		CGPURenderPassDescriptor _descriptor;
	};

	struct RenderPassDescriptorHasher
	{
		inline size_t operator()(const CGPURenderPassDescriptor& a) const
		{
			return MurmurHashFn<CGPURenderPassDescriptor>()(a);
		}
	};

	struct RenderPassDescriptorEq
	{
		inline bool operator()(const CGPURenderPassDescriptor& a, const CGPURenderPassDescriptor& b) const
		{
			return !(bool)memcmp(&a, &b, sizeof(CGPURenderPassDescriptor));
		}
	};

	class RenerPassPool
		: public ResourcePool<CGPURenderPassDescriptor, RenderPass, true, true, RenderPassDescriptorHasher, RenderPassDescriptorEq>
	{
	public:
		RenerPassPool(CGPUDeviceId device, std::pmr::memory_resource* const memory_resource);

		RenderPass* getRenderPass(const CGPURenderPassDescriptor& descriptor);

	protected:
		// Í¨¹ý ResourcePool ¼Ì³Ð
		virtual RenderPass* getResource_impl(const CGPURenderPassDescriptor& descriptor) override;
		virtual void destroyResource_impl(RenderPass* resource) override;

	private:
		CGPUDeviceId device{ CGPU_NULLPTR };
		std::pmr::polymorphic_allocator<> allocator;
	};
}