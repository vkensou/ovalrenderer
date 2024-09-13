#pragma once

#include "cgpu/api.h"
#include "resourcepool.h"
#include "hash.h"

namespace HGEGraphics
{
	struct DescriptorSet
	{
		CGPUDescriptorSetDescriptor descriptor() const
		{
			return _descriptor;
		}
		CGPUDescriptorSetId handle;
		CGPUDescriptorSetDescriptor _descriptor;
	};

	struct CGPUDescriptorSetDescriptorHasher
	{
		inline size_t operator()(const CGPUDescriptorSetDescriptor& a) const
		{
			return MurmurHashFn<CGPUDescriptorSetDescriptor>()(a);
		}
	};

	struct CGPUDescriptorSetDescriptorEq
	{
		inline bool operator()(const CGPUDescriptorSetDescriptor& a, const CGPUDescriptorSetDescriptor& b) const
		{
			return !(bool)memcmp(&a, &b, sizeof(CGPUDescriptorSetDescriptor));
		}
	};

	class DescriptorSetPool
		: public ResourcePool<CGPUDescriptorSetDescriptor, DescriptorSet, false, true, CGPUDescriptorSetDescriptorHasher, CGPUDescriptorSetDescriptorEq>
	{
	public:
		DescriptorSetPool(CGPUDeviceId device, std::pmr::memory_resource* const memory_resource);

		DescriptorSet* getDescriptorSet(const CGPUDescriptorSetDescriptor& descriptor);

	protected:
		// Í¨¹ý ResourcePool ¼Ì³Ð
		virtual DescriptorSet* getResource_impl(const CGPUDescriptorSetDescriptor& descriptor) override;
		virtual void destroyResource_impl(DescriptorSet* resource) override;

	private:
		CGPUDeviceId device{ CGPU_NULLPTR };
		std::pmr::polymorphic_allocator<> allocator;
	};
}