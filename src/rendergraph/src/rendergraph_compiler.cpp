#include "rendergraph_compiler.h"

#include "dependencygraph.h"
#include <cassert>
#include <algorithm>
#include "renderer.h"

namespace HGEGraphics
{
	CompiledRenderGraph Compiler::Compile(const rendergraph_t& renderGraph, std::pmr::memory_resource* const memory_resource)
	{
		auto resourceCount = renderGraph.resources.size();
		auto passCount = renderGraph.passes.size();
		auto edgeCount = renderGraph.edges.size();

		HGEGraphics::DependencyGraph graph(resourceCount + passCount, edgeCount, memory_resource);
		
		struct Node
		{
			Node(uint16_t index, bool is_pass, bool is_persistent, std::pmr::memory_resource* const resource)
				: index(index), is_pass(is_pass), is_persistent(is_persistent), ins(resource), outs(resource)
			{
			}

			bool is_culled() const
			{
				return ref_count == 0 && !is_persistent;
			}

			std::pmr::vector<uint16_t> ins;
			std::pmr::vector<uint16_t> outs;
			uint16_t index = 0;
			uint16_t ref_count = 0;
			bool is_pass;
			bool is_persistent{ false };
		};

		std::pmr::vector<Node> nodes(memory_resource);
		nodes.reserve(passCount + resourceCount);
		for (uint16_t i = 0; i < passCount; ++i)
		{
			auto const& pass = renderGraph.passes[i];
			Node node(i, true, pass.type == PASS_TYPE_PRESENT || pass.type == PASS_TYPE_HOLDON, memory_resource);
			node.ins.reserve(pass.reads.size());
			for (uint16_t j = 0; j < pass.reads.size(); ++j)
				node.ins.push_back(renderGraph.edges[pass.reads[j]].from + passCount);
			node.outs.reserve(pass.writes.size());
			for (uint16_t j = 0; j < pass.writes.size(); ++j)
				node.outs.push_back(renderGraph.edges[pass.writes[j]].to + passCount);
			nodes.emplace_back(std::move(node));
		}

		for (uint16_t i = 0; i < resourceCount; ++i)
		{
			auto const& resource = renderGraph.resources[i];
			Node node(i, false, resource.manageType != ManageType::Managed, memory_resource);
			nodes.emplace_back(std::move(node));
		}

		for (uint16_t i = 0; i < passCount; ++i)
		{
			auto const& node = nodes[i];
			for (uint16_t j = 0; j < node.ins.size(); ++j)
			{
				auto the_in = node.ins[j];
				nodes[the_in].outs.push_back(i);
			}
			for (uint16_t j = 0; j < node.outs.size(); ++j)
			{
				auto the_out = node.outs[j];
				nodes[the_out].ins.push_back(i);
			}
		}

		for (auto i = 0; i < nodes.size(); ++i)
		{
			auto& node = nodes[i];
			node.ref_count = node.outs.size();
		}

		std::pmr::vector<uint16_t> cullingStack(memory_resource);
		cullingStack.reserve(nodes.size());
		for (auto i = 0; i < nodes.size(); ++i)
		{
			auto& node = nodes[i];
			if (node.ref_count == 0 && !node.is_persistent)
				cullingStack.push_back(i);
		}

		while (!cullingStack.empty())
		{
			auto index = cullingStack.back();
			cullingStack.pop_back();
			auto const& node = nodes[index];

			for (auto i = 0; i < node.ins.size(); ++i)
			{
				auto inNodeIndex = node.ins[i];
				auto& inNode = nodes[inNodeIndex];
				assert(inNode.ref_count > 0);
				inNode.ref_count--;
				if (inNode.ref_count == 0 && !inNode.is_persistent)
					cullingStack.push_back(inNodeIndex);
			}
		}

		CompiledRenderGraph compiled(memory_resource);
		auto usedPassCount = std::count_if(nodes.begin(), nodes.begin() + passCount, [](auto& node) {return !node.is_culled(); });
		auto usedResourceCount = std::count_if(nodes.begin() + passCount, nodes.end(), [](auto& node) {return !node.is_culled(); });
		compiled.passes.reserve(usedPassCount);
		for (auto i = 0; i < passCount; ++i)
		{
			auto const& node = nodes[i];
			auto const& pass = renderGraph.passes[node.index];
			if (node.is_culled())
				continue;

			auto& compiledPass = compiled.passes.emplace_back(pass.name, memory_resource);
			compiledPass.type = pass.type;

			compiledPass.reads.reserve(pass.reads.size());
			for (auto edgeIndex : pass.reads)
			{
				auto& edge = renderGraph.edges[edgeIndex];
				compiledPass.reads.emplace_back(edge.from, edge.usage);
			}

			compiledPass.writes.reserve(pass.writes.size());
			for (auto edgeIndex : pass.writes)
			{
				auto& edge = renderGraph.edges[edgeIndex];
				compiledPass.writes.emplace_back(edge.to, edge.usage);
			}

			if (pass.type == PASS_TYPE_RENDER)
			{
				compiledPass.colorAttachmentCount = pass.render_context.colorAttachmentCount;
				for (auto j = 0; j < pass.render_context.colorAttachmentCount; ++j)
				{
					compiledPass.colorAttachments[j] = pass.render_context.colorAttachments[j];
				}
				compiledPass.depthAttachment = pass.render_context.depthAttachment;
				compiledPass.executable = pass.render_context.executable;
			}
			else if (pass.type == PASS_TYPE_COMPUTE)
			{
				compiledPass.executable = pass.compute_context.executable;
			}
			else if (pass.type == PASS_TYPE_UPLOAD_TEXTURE)
			{
				compiledPass.staging_buffer = pass.upload_texture_context.staging_buffer.index().value();
				compiledPass.dest_texture = pass.upload_texture_context.dest_texture.index().value();
				compiledPass.uploadTextureExecutable = pass.upload_texture_context.executable;
				compiledPass.size = pass.upload_texture_context.size;
				compiledPass.offset = pass.upload_texture_context.offset;
				compiledPass.data = pass.upload_texture_context.data;
				compiledPass.mipmap = pass.upload_texture_context.mipmap;
				compiledPass.slice = pass.upload_texture_context.slice;
			}
			else if (pass.type == PASS_TYPE_UPLOAD_BUFFER)
			{
				compiledPass.staging_buffer = pass.upload_buffer_context.staging_buffer.index().value();
				compiledPass.dest_buffer = pass.upload_buffer_context.dest_buffer.index().value();
				compiledPass.uploadTextureExecutable = pass.upload_buffer_context.executable;
				compiledPass.size = pass.upload_buffer_context.size;
				compiledPass.offset = pass.upload_buffer_context.offset;
				compiledPass.data = pass.upload_buffer_context.data;
			}
			compiledPass.passdata = pass.passdata;
		}

		compiled.resources.reserve(usedResourceCount);
		for (auto i = 0; i < resourceCount; ++i)
		{
			auto const& node = nodes[i + passCount];
			auto const& resource = renderGraph.resources[node.index];
			if (node.is_culled())
				continue;

			if (resource.resourceType == ResourceType::Texture)
				compiled.resources.emplace_back(resource.name, resource.manageType, resource.width, resource.height, resource.depth, resource.format, resource.texture, resource.mipCount, resource.arraySize, resource.parent, resource.mipLevel, resource.arraySlice);
			else if (resource.resourceType == ResourceType::Buffer)
				compiled.resources.emplace_back(resource.name, resource.manageType, resource.size, resource.buffer, resource.bufferType, resource.memoryUsage);

			if (resource.manageType == ManageType::Managed)
			{
				auto first = UINT16_MAX;
				uint16_t last = 0;
				if (!node.ins.empty())
				{
					first = std::min(first, node.ins.front());
					last = std::max(last, node.ins.back());
				}
				if (!node.outs.empty())
				{
					first = std::min(first, node.outs.front());
					last = std::max(last, node.outs.back());
				}
				if (resource.holdOnLast)
					last = passCount - 1;

				assert(first >= 0 && first < compiled.passes.size());
				compiled.passes[first].devirtualize.push_back(i);
				assert(last >= 0 && last < compiled.passes.size());
				compiled.passes[last].destroy.push_back(i);
			}
		}

		return compiled;
	}
	CompiledResourceNode::CompiledResourceNode(const char8_t* name, ManageType type, uint16_t width, uint16_t height, uint16_t depth, ECGPUFormat format, Texture* imported_texture, uint8_t mipCount, uint8_t arraySize, uint16_t parent, uint8_t mipLevel, uint8_t arraySlice)
		: name(name), resourceType(ResourceType::Texture), manageType(type), width(width), height(height), depth(depth), format(format), imported_texture(imported_texture), imported_buffer(CGPU_NULLPTR), managered_texture(nullptr), size(0), managed_buffer(nullptr), bufferType(CGPU_RESOURCE_TYPE_NONE), memoryUsage(CGPU_MEM_USAGE_UNKNOWN)
		, mipCount(mipCount), arraySize(arraySize), parent(parent), mipLevel(mipLevel), arraySlice(arraySlice)
	{
	}
	CompiledResourceNode::CompiledResourceNode(const char8_t* name, ManageType type, uint32_t size, Buffer* imported_buffer, CGPUResourceTypes bufferType, ECGPUMemoryUsage memoryUsage)
		: name(name), resourceType(ResourceType::Buffer), manageType(type), size(size), width(0), height(0), depth(depth), format(CGPU_FORMAT_UNDEFINED), imported_texture(CGPU_NULLPTR), imported_buffer(imported_buffer), managered_texture(nullptr), managed_buffer(nullptr), bufferType(bufferType), memoryUsage(memoryUsage)
		, mipCount(0), arraySize(0), parent(0), mipLevel(0), arraySlice(0)
	{
	}
	CompiledRenderPassNode::CompiledRenderPassNode(const char8_t* name, std::pmr::memory_resource* const memory_resource)
		: name(name), reads(memory_resource), writes(memory_resource)
	{
	}
	CompiledRenderGraph::CompiledRenderGraph(std::pmr::memory_resource* const memory_resource)
		: passes(memory_resource), resources(memory_resource)
	{
	}

	CGPUBufferId rendergraph_resolve_buffer(RenderPassEncoder* encoder, uint16_t buffer_handle)
	{
		auto crg = encoder->compiled_graph;
		auto resourceNode = crg->resources[buffer_handle];
		auto buffer = resourceNode.manageType == ManageType::Managed ? resourceNode.managed_buffer->handle : resourceNode.imported_buffer->handle;
		return buffer;
	}

	CGPUTextureViewId rendergraph_resolve_texture_view(RenderPassEncoder* encoder, uint16_t texture_handle)
	{
		auto crg = encoder->compiled_graph;
		auto& resourceNode = crg->resources[texture_handle];
		CGPUTextureViewDescriptor desc = {};
		Texture* texture;
		if (resourceNode.manageType == ManageType::Managed)
			texture = resourceNode.managered_texture->texture;
		else if (resourceNode.manageType == ManageType::Imported)
			texture = resourceNode.imported_texture;
		else
		{
			auto& parentResource = crg->resources[resourceNode.parent];
			assert(parentResource.parent == 0 && parentResource.resourceType == ResourceType::Texture);
			texture = parentResource.manageType == ManageType::Managed ? parentResource.managered_texture->texture : parentResource.imported_texture;
		}
		desc.texture = texture->handle;
		desc.format = texture->handle->info->format;
		desc.usages = CGPU_TVU_SRV;
		desc.aspects = CGPU_TVA_COLOR;
		desc.dims = texture->handle->info->depth > 1 ? CGPU_TEX_DIMENSION_3D :  CGPU_TEX_DIMENSION_2D;
		desc.base_array_layer = resourceNode.arraySlice;
		desc.array_layer_count = resourceNode.manageType != ManageType::SubResource ? texture->handle->info->array_size_minus_one + 1 : 1;
		desc.base_mip_level = resourceNode.mipLevel;
		desc.mip_level_count = resourceNode.manageType != ManageType::SubResource ? texture->handle->info->mip_levels : 1;
		auto textureView = encoder->context->textureViewPool.getResource(desc);
		return textureView->handle;
	}
}