#pragma once

#include "resourcepool.h"
#include "cgpu/api.h"
#include "hash.h"

namespace HGEGraphics
{
	struct BufferWrap
	{
		CGPUBufferDescriptor descriptor() const
		{
			return _descriptor;
		}
		CGPUBufferId handle;
		CGPUBufferDescriptor _descriptor;
		ECGPUResourceState cur_state;
	};

	struct BufferDescriptorHasher
	{
		inline size_t operator()(const CGPUBufferDescriptor& a) const
		{
			return MurmurHashFn<CGPUBufferDescriptor>()(a);
		}
	};

	struct BufferDescriptorEq
	{
		inline bool operator()(const CGPUBufferDescriptor& a, const CGPUBufferDescriptor& b) const
		{
			return !(bool)memcmp(&a, &b, sizeof(CGPUBufferDescriptor));
		}
	};

	class BufferPool
		: public ResourcePool<CGPUBufferDescriptor, BufferWrap, false, true, BufferDescriptorHasher, BufferDescriptorEq>
	{
	public:
		BufferPool(CGPUDeviceId device, BufferPool* upstream, std::pmr::memory_resource* const memory_resource);

	protected:
		BufferWrap* getResource_impl(const CGPUBufferDescriptor& descriptor) override;
		void destroyResource_impl(BufferWrap* resource) override;

	private:
		CGPUDeviceId device{ CGPU_NULLPTR };
		std::pmr::polymorphic_allocator<> allocator;
	};
}