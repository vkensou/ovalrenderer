#pragma once

#include "resourcepool.h"
#include "cgpu/api.h"
#include "hash.h"
#include <string.h>

namespace HGEGraphics
{
	struct Shader;
	struct Mesh;
	struct RenderPassEncoder;

	struct PSOKey
	{
		Shader* shader;
		CGPUVertexLayout vertex_layout;
		ECGPUPrimitiveTopology prim_topology;
		CGPUBlendStateDescriptor blend_desc;
		CGPUDepthStateDesc depth_desc;
		CGPURasterizerStateDescriptor rasterizer_state;
		CGPURenderPassId render_pass;
		uint32_t subpass;
		uint32_t render_target_count;
	};

	struct PSOKeyHasher
	{
		inline size_t operator()(const PSOKey& a) const
		{
			return MurmurHashFn<PSOKey>()(a);
		}
	};

	struct PSOKeyEq
	{
		inline bool operator()(const PSOKey& a, const PSOKey& b) const
		{
			return !(bool)memcmp(&a, &b, sizeof(PSOKey));
		}
	};

	struct GraphicsPipeline
	{
		PSOKey descriptor() const
		{
			return _descriptor;
		}
		CGPURenderPipelineId handle;
		PSOKey _descriptor;
	};

	class GraphicsPipelinePool
		: public ResourcePool<PSOKey, GraphicsPipeline, true, true, PSOKeyHasher, PSOKeyEq>
	{
	public:
		GraphicsPipelinePool(CGPUDeviceId device, GraphicsPipelinePool* upstream, std::pmr::memory_resource* const memory_resource);

		GraphicsPipeline* getGraphicsPipeline(RenderPassEncoder* encoder, Shader* shader, Mesh* mesh);
		GraphicsPipeline* getGraphicsPipeline(RenderPassEncoder* encoder, Shader* shader, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout);

		// Í¨¹ý ResourcePool ¼Ì³Ð
		virtual GraphicsPipeline* getResource_impl(const PSOKey& descriptor) override;

		virtual void destroyResource_impl(GraphicsPipeline* resource) override;

		bool dynamicStateT1Enabled() const { return dynamic_state_t1; }
		bool dynamicStateT2Enabled() const { return dynamic_state_t2; }
		bool dynamicStateT3Enabled() const { return dynamic_state_t3; }

	private:
		CGPUDeviceId device{ CGPU_NULLPTR };
		CGPUDynamicStateFeatures _dynamic_state_features{ 0 };
		bool dynamic_state_t1{ false };
		bool dynamic_state_t2{ false };
		bool dynamic_state_t3{ false };
		std::pmr::polymorphic_allocator<> allocator;
	};
}