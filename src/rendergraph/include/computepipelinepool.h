#pragma once

#include "resourcepool.h"
#include "cgpu/api.h"
#include "hash.h"
#include <string.h>

namespace HGEGraphics
{
	struct ComputeShader;

	struct CPSOKey
	{
		ComputeShader* shader;
	};

	struct CPSOKeyHasher
	{
		inline size_t operator()(const CPSOKey& a) const
		{
			return MurmurHashFn<CPSOKey>()(a);
		}
	};

	struct CPSOKeyEq
	{
		inline bool operator()(const CPSOKey& a, const CPSOKey& b) const
		{
			return !(bool)memcmp(&a, &b, sizeof(CPSOKey));
		}
	};

	struct ComputePipeline
	{
		CPSOKey descriptor() const
		{
			return _descriptor;
		}
		CGPUComputePipelineId handle;
		CPSOKey _descriptor;
	};

	class ComputePipelinePool
		: public ResourcePool<CPSOKey, ComputePipeline, true, true, CPSOKeyHasher, CPSOKeyEq>
	{
	public:
		ComputePipelinePool(CGPUDeviceId device, ComputePipelinePool* upstream, std::pmr::memory_resource* const memory_resource);

		ComputePipeline* getComputePipeline(ComputeShader* shader);

		// Í¨¹ý ResourcePool ¼Ì³Ð
		virtual ComputePipeline* getResource_impl(const CPSOKey& descriptor) override;

		virtual void destroyResource_impl(ComputePipeline* resource) override;

	private:
		CGPUDeviceId device{ CGPU_NULLPTR };
		std::pmr::polymorphic_allocator<> allocator;
	};
}