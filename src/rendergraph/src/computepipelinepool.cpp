#include "computepipelinepool.h"

#include "renderer.h"

namespace HGEGraphics
{
	ComputePipelinePool::ComputePipelinePool(CGPUDeviceId device, ComputePipelinePool* upstream, std::pmr::memory_resource* const memory_resource)
		: device(device), ResourcePool(12, upstream, memory_resource), allocator(memory_resource)
	{
	}

	ComputePipeline* ComputePipelinePool::getComputePipeline(ComputeShader* shader)
	{
		auto key = CPSOKey
		{
			.shader = shader,
		};
		return getResource(key);
	}

	ComputePipeline* ComputePipelinePool::getResource_impl(const CPSOKey& key)
	{
		CGPUComputePipelineDescriptor cp_desc = {
			.root_signature = key.shader->root_sig,
			.compute_shader = &key.shader->cs,
		};
		auto handle = cgpu_create_compute_pipeline(device, &cp_desc);

		auto pipeline = allocator.new_object<ComputePipeline>();
		pipeline->handle = handle;
		return pipeline;
	}

	void ComputePipelinePool::destroyResource_impl(ComputePipeline* resource)
	{
		cgpu_free_compute_pipeline(resource->handle);
		allocator.delete_object(resource);
	}
}