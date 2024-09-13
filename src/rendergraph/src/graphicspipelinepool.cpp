#include "GraphicsPipelinePool.h"

#include "renderer.h"

namespace HGEGraphics
{
	GraphicsPipelinePool::GraphicsPipelinePool(CGPUDeviceId device, GraphicsPipelinePool* upstream, std::pmr::memory_resource* const memory_resource)
		: device(device), ResourcePool(12, upstream, memory_resource), allocator(memory_resource)
	{
		if (device)
		{
			auto adapter_detail = cgpu_query_adapter_detail(device->adapter);
			_dynamic_state_features = adapter_detail->dynamic_state_features;
			dynamic_state_t1 = (_dynamic_state_features & CGPU_DYNAMIC_STATE_Tier1) != 0;
			dynamic_state_t2 = (_dynamic_state_features & CGPU_DYNAMIC_STATE_Tier2) != 0;
			dynamic_state_t3 = (_dynamic_state_features & CGPU_DYNAMIC_STATE_Tier3) != 0;
		}
	}
	GraphicsPipeline* GraphicsPipelinePool::getGraphicsPipeline(RenderPassEncoder* encoder, Shader* shader, Mesh* mesh)
    {
		return getGraphicsPipeline(encoder, shader, mesh->prim_topology, mesh->vertex_layout);
	}

	GraphicsPipeline* GraphicsPipelinePool::getGraphicsPipeline(RenderPassEncoder* encoder, Shader* shader, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout)
	{
		auto key = PSOKey
		{
			.shader = shader,
			.vertex_layout = vertex_layout,
			.prim_topology = prim_topology,
			.blend_desc = shader->blend_desc,
			.depth_desc = shader->depth_desc,
			.rasterizer_state = shader->rasterizer_state,
			.render_pass = encoder->render_pass,
			.subpass = encoder->subpass,
			.render_target_count = encoder->render_target_count,
		};
		if (dynamicStateT1Enabled())
		{
			key.prim_topology = (ECGPUPrimitiveTopology)0;
			key.rasterizer_state.cull_mode = (ECGPUCullMode)0;
			key.rasterizer_state.front_face = (ECGPUFrontFace)0;
			key.depth_desc.depth_test = false;
			key.depth_desc.depth_write = false;
			key.depth_desc.depth_func = (ECGPUCompareMode)0;
		}
		return getResource(key);
	}

	GraphicsPipeline* GraphicsPipelinePool::getResource_impl(const PSOKey& key)
	{
		CGPURenderPipelineDescriptor rp_desc = {
			.dynamic_state = _dynamic_state_features,
			.root_signature = key.shader->root_sig,
			.vertex_shader = &key.shader->vs,
			.fragment_shader = &key.shader->ps,
			.vertex_layout = &key.vertex_layout,
			.blend_state = &key.blend_desc,
			.depth_state = &key.depth_desc,
			.rasterizer_state = &key.rasterizer_state,
			.render_pass = key.render_pass,
			.subpass = key.subpass,
			.render_target_count = key.render_target_count,
			.prim_topology = key.prim_topology,
		};
		auto handle = cgpu_create_render_pipeline(device, &rp_desc);

		auto pipeline = allocator.new_object<GraphicsPipeline>();
		pipeline->handle = handle;
		return pipeline;
	}

    void GraphicsPipelinePool::destroyResource_impl(GraphicsPipeline* resource)
    {
		cgpu_free_render_pipeline(resource->handle);
		allocator.delete_object(resource);
    }
}