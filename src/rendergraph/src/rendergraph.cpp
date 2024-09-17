#include "rendergraph.h"

#include <cassert>
#include "renderer.h"

namespace HGEGraphics
{
	inline texture_handle_t make_texture_handle(uint32_t index)
	{
		texture_handle_t handle;
		handle.index = index;
		return handle;
	}

	inline buffer_handle_t make_buffer_handle(uint32_t index)
	{
		buffer_handle_t handle;
		handle.index = index;
		return handle;
	}

	inline bool is_valid_dynamic_texture_handle(std::pmr::vector<ResourceNode>& resources, texture_handle_t handle)
	{
		return rendergraph_texture_handle_valid(handle) && handle.index < resources.size();
	}

	inline bool is_valid_dynamic_buffer_handle(std::pmr::vector<ResourceNode>& resources, buffer_handle_t handle)
	{
		return rendergraph_buffer_handle_valid(handle) && handle.index < resources.size();
	}

	rendergraph_t::rendergraph_t(size_t estimate_resource_count, size_t estimate_pass_count, size_t estimate_edge_count, Shader* blitShader, CGPUSamplerId blitSampler, std::pmr::memory_resource* const resource)
		: allocator(resource), resources(resource), passes(resource), edges(resource), blitShader(blitShader), blitSampler(blitSampler)
	{
		resources.reserve(estimate_resource_count);
		resources.push_back({});
		passes.reserve(estimate_pass_count);
		edges.reserve(estimate_edge_count);
	}
	void rendergraph_reset(rendergraph_t* self)
	{
		self->resources.clear();
		self->passes.clear();
		self->edges.clear();
	}
	void allocate_passdata(rendergraph_t* self, RenderPassNode* passNode, size_t passdata_size, void** passdata)
	{
		if (passdata_size > 0)
		{
			auto pd = self->allocator.allocate_bytes(passdata_size);
			passNode->passdata = *passdata = pd;
		}
		else
			passNode->passdata = nullptr;
	}
	renderpass_builder_t rendergraph_add_renderpass(rendergraph_t* self, const char8_t* name)
	{
		assert(self->passes.size() <= MAX_INDEX);
		self->passes.emplace_back(name, PASS_TYPE_RENDER, self->allocator.resource());
		return renderpass_builder_t(self, &(self->passes.back()), self->passes.size() - 1);
	}
	renderpass_builder_t rendergraph_add_computepass(rendergraph_t* self, const char8_t* name)
	{
		assert(self->passes.size() <= MAX_INDEX);
		self->passes.emplace_back(name, PASS_TYPE_COMPUTE, self->allocator.resource());
		return renderpass_builder_t(self, &(self->passes.back()), self->passes.size() - 1);
	}
	renderpass_builder_t rendergraph_add_holdpass(rendergraph_t* self, const char8_t* name)
	{
		assert(self->passes.size() <= MAX_INDEX);
		self->passes.emplace_back(name, PASS_TYPE_HOLDON, self->allocator.resource());
		return renderpass_builder_t(self, &(self->passes.back()), self->passes.size() - 1);
	}
	void rendergraph_add_uploadtexturepass(rendergraph_t* self, const char8_t* name, texture_handle_t texture, uint8_t mipmap, uint8_t slice, uploadpass_executable executable, size_t passdata_size, void** passdata)
	{
		rendergraph_add_uploadtexturepass_ex(self, name, texture, mipmap, slice, 0, 0, nullptr, executable, passdata_size, passdata);
	}
	void rendergraph_add_uploadtexturepass_ex(rendergraph_t* self, const char8_t* name, texture_handle_t texture, uint8_t mipmap, uint8_t slice, uint64_t size, uint64_t offset, void* data, uploadpass_executable executable, size_t passdata_size, void** passdata)
	{
		assert(self->passes.size() <= MAX_INDEX);
		auto& pass = self->passes.emplace_back(name, PASS_TYPE_UPLOAD_TEXTURE, self->allocator.resource());
		int passIndex = self->passes.size() - 1;

		assert(rendergraph_texture_handle_valid(texture));
		auto& textureNode = self->resources[texture.index];
		assert(textureNode.resourceType == ResourceType::Texture);

		texture_handle_t usedTexture;
		ResourceNode* usedTextureNode;

		if (textureNode.mipCount == 1 && textureNode.arraySize == 1)
		{
			usedTexture = texture;
			usedTextureNode = &textureNode;
		}
		else
		{
			usedTexture = rendergraph_declare_texture_subresource(self, texture, mipmap, slice);
			usedTextureNode = &self->resources[usedTexture.index];
		}

		pass.upload_texture_context.dest_texture = usedTexture;
		auto write_edge = rendergraph_add_edge(self, passIndex, usedTexture.index, CGPU_RESOURCE_STATE_COPY_DEST);
		pass.writes.push_back(write_edge);

		auto staging_buffer = rendergraph_declare_buffer(self);
		auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max(size >> mip, 1ull); };
		const uint64_t xBlocksCount = mipedSize(usedTextureNode->width, mipmap) / FormatUtil_WidthOfBlock(usedTextureNode->format);
		const uint64_t yBlocksCount = mipedSize(usedTextureNode->height, mipmap) / FormatUtil_HeightOfBlock(usedTextureNode->format);
		const uint64_t zBlocksCount = mipedSize(usedTextureNode->depth, mipmap);
		const uint64_t bufferSize = xBlocksCount * yBlocksCount * zBlocksCount * FormatUtil_BitSizeOfBlock(usedTextureNode->format) / sizeof(uint8_t);
		assert(bufferSize >= size + offset);
		rg_buffer_set_size(self, staging_buffer, bufferSize);
		rg_buffer_set_type(self, staging_buffer, CGPU_RESOURCE_TYPE_NONE);
		rg_buffer_set_usage(self, staging_buffer, CGPU_MEM_USAGE_CPU_ONLY);
		rg_buffer_set_hold_on_last(self, staging_buffer);
		pass.upload_texture_context.staging_buffer = staging_buffer;
		auto read_edge = rendergraph_add_edge(self, staging_buffer.index, passIndex, CGPU_RESOURCE_STATE_COPY_SOURCE);
		pass.reads.push_back(read_edge);

		pass.upload_texture_context.executable = executable;
		allocate_passdata(self, &pass, passdata_size, passdata);
		pass.upload_texture_context.size = size;
		pass.upload_texture_context.offset = offset;
		pass.upload_texture_context.data = data;
		pass.upload_texture_context.mipmap = mipmap;
		pass.upload_texture_context.slice = slice;
	}
	void rendergraph_add_uploadbufferpass(rendergraph_t* self, const char8_t* name, buffer_handle_t buffer, uploadpass_executable executable, size_t passdata_size, void** passdata)
	{
		rendergraph_add_uploadbufferpass_ex(self, name, buffer, 0, 0, nullptr, executable, passdata_size, passdata);
	}
	void rendergraph_add_uploadbufferpass_ex(rendergraph_t* self, const char8_t* name, buffer_handle_t buffer, uint64_t size, uint64_t offset, void* data, uploadpass_executable executable, size_t passdata_size, void** passdata)
	{
		assert(self->passes.size() <= MAX_INDEX);
		auto& pass = self->passes.emplace_back(name, PASS_TYPE_UPLOAD_BUFFER, self->allocator.resource());
		int passIndex = self->passes.size() - 1;

		assert(rendergraph_buffer_handle_valid(buffer));
		auto& resourceNode = self->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);
		pass.upload_buffer_context.dest_buffer = buffer;
		auto write_edge = rendergraph_add_edge(self, passIndex, buffer.index, CGPU_RESOURCE_STATE_COPY_DEST);
		pass.writes.push_back(write_edge);

		auto staging_buffer = rendergraph_declare_buffer(self);
		assert(resourceNode.size >= size + offset);
		rg_buffer_set_size(self, staging_buffer, resourceNode.size);
		rg_buffer_set_type(self, staging_buffer, CGPU_RESOURCE_TYPE_NONE);
		rg_buffer_set_usage(self, staging_buffer, CGPU_MEM_USAGE_CPU_ONLY);
		rg_buffer_set_hold_on_last(self, staging_buffer);
		pass.upload_buffer_context.staging_buffer = staging_buffer;
		auto read_edge = rendergraph_add_edge(self, staging_buffer.index, passIndex, CGPU_RESOURCE_STATE_COPY_SOURCE);
		pass.reads.push_back(read_edge);

		pass.upload_buffer_context.executable = executable;
		allocate_passdata(self, &pass, passdata_size, passdata);
		pass.upload_buffer_context.size = size;
		pass.upload_buffer_context.offset = offset;
		pass.upload_buffer_context.data = data;
	}
	void rendergraph_add_generate_mipmap(rendergraph_t* self, texture_handle_t texture)
	{
		assert(rendergraph_texture_handle_valid(texture));
		auto& textureNode = self->resources[texture.index];
		assert(textureNode.resourceType == ResourceType::Texture && textureNode.manageType != ManageType::SubResource);
		assert(textureNode.arraySize == 1);
		if (textureNode.mipCount == 1)
			return;

		auto mip0 = rendergraph_declare_texture_subresource(self, texture, 0, 0);
		auto last = mip0;
		for (size_t i = 1; i < textureNode.mipCount; ++i)
		{
			auto mipi = rendergraph_declare_texture_subresource(self, texture, i, 0);

			auto passBuilder = rendergraph_add_renderpass(self, u8"generate mip");
			renderpass_add_color_attachment(&passBuilder, mipi, CGPU_LOAD_ACTION_DONTCARE, 0, CGPU_STORE_ACTION_STORE);
			renderpass_sample(&passBuilder, last);

			struct BlitMipmapPassData
			{
				Shader* blitShader;
				CGPUSamplerId blitSampler;
				texture_handle_t source;
			};
			BlitMipmapPassData* passdata = nullptr;
			renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* passdata)
				{
					BlitMipmapPassData* resolved_passdata = (BlitMipmapPassData*)passdata;
					set_global_texture_handle(encoder, resolved_passdata->source, 0, 0);
					set_global_sampler(encoder, resolved_passdata->blitSampler, 0, 1);
					draw_procedure(encoder, resolved_passdata->blitShader, CGPU_PRIM_TOPO_TRI_LIST, 3);
				}, sizeof(BlitMipmapPassData), (void**)&passdata);
			passdata->blitShader = self->blitShader;
			passdata->blitSampler = self->blitSampler;
			passdata->source = last;
			last = mipi;
		}
	}
	void rendergraph_present(rendergraph_t* self, texture_handle_t texture)
	{
		assert(self->passes.size() <= MAX_INDEX);
		auto& passNode = self->passes.emplace_back(u8"Present", PASS_TYPE_PRESENT, self->allocator.resource());
		int passIndex = self->passes.size() - 1;
		auto edge = rendergraph_add_edge(self, texture.index, passIndex, CGPU_RESOURCE_STATE_PRESENT);
		passNode.reads.push_back(edge);
	}
	texture_handle_t rendergraph_declare_texture(rendergraph_t* self)
	{
		assert(self->resources.size() <= MAX_INDEX);
		self->resources.push_back(ResourceNode());
		auto& resourceNode = self->resources.back();
		resourceNode.width = 0;
		resourceNode.height = 0;
		resourceNode.depth = 0;
		resourceNode.mipCount = 1;
		resourceNode.arraySize = 1;
		resourceNode.mipLevel = 0;
		resourceNode.arraySlice = 0;
		return make_texture_handle(self->resources.size() - 1);
	}
	texture_handle_t rendergraph_import_texture(rendergraph_t* self, Texture* imported)
	{
		assert(self->resources.size() <= MAX_INDEX);
		self->resources.push_back(ResourceNode());
		auto& resourceNode = self->resources.back();
		resourceNode.texture = imported;
		resourceNode.manageType = ManageType::Imported;
		resourceNode.width = imported->handle->info->width;
		resourceNode.height = imported->handle->info->height;
		resourceNode.depth = imported->handle->info->depth;
		resourceNode.format = imported->handle->info->format;
		resourceNode.mipCount = imported->handle->info->mip_levels;
		resourceNode.arraySize = imported->handle->info->array_size_minus_one + 1;
		resourceNode.mipLevel = 0;
		resourceNode.arraySlice = 0;
		return make_texture_handle(self->resources.size() - 1);
	}
	texture_handle_t rendergraph_import_backbuffer(rendergraph_t* self, Backbuffer* imported)
	{
		assert(self->resources.size() <= MAX_INDEX);
		self->resources.push_back(ResourceNode());
		auto& resourceNode = self->resources.back();
		imported->texture.cur_states[0] = CGPU_RESOURCE_STATE_UNDEFINED;
		imported->texture.states_consistent = true;
		resourceNode.texture = &(imported->texture);
		resourceNode.manageType = ManageType::Imported;
		resourceNode.width = imported->texture.handle->info->width;
		resourceNode.height = imported->texture.handle->info->height;
		resourceNode.depth = imported->texture.handle->info->depth;
		resourceNode.format = imported->texture.handle->info->format;
		resourceNode.mipCount = imported->texture.handle->info->mip_levels;
		resourceNode.arraySize = imported->texture.handle->info->array_size_minus_one + 1;
		resourceNode.mipLevel = 0;
		resourceNode.arraySlice = 0;
		return make_texture_handle(self->resources.size() - 1);
	}
	buffer_handle_t rendergraph_declare_buffer(rendergraph_t* self)
	{
		assert(self->resources.size() <= MAX_INDEX);
		self->resources.push_back(ResourceNode());
		auto& resource = self->resources.back();
		resource.resourceType = ResourceType::Buffer;
		resource.width = 0;
		resource.memoryUsage = CGPU_MEM_USAGE_UNKNOWN;
		return make_buffer_handle(self->resources.size() - 1);
	}
	buffer_handle_t rendergraph_declare_uniform_buffer_quick(rendergraph_t* self, uint32_t size, void* data)
	{
		assert(self->resources.size() <= MAX_INDEX);
		auto nextPowerOfTwo = [](uint32_t n) -> uint32_t
			{
				if (n == 0)
				{
					return 1;
				}

				// Decrement n to handle the exact power of 2 case.
				n--;

				// Set all bits after the highest set bit
				n |= n >> 1;
				n |= n >> 2;
				n |= n >> 4;
				n |= n >> 8;
				n |= n >> 16;

				// Increment n to get next power of 2
				return n + 1;
			};

		self->resources.push_back(ResourceNode());
		auto& resource = self->resources.back();
		resource.resourceType = ResourceType::Buffer;
		resource.memoryUsage = CGPU_MEM_USAGE_UNKNOWN;
		resource.size = nextPowerOfTwo(size);
		resource.bufferType = CGPU_RESOURCE_TYPE_UNIFORM_BUFFER;
		resource.memoryUsage = ECGPUMemoryUsage::CGPU_MEM_USAGE_GPU_ONLY;
		buffer_handle_t ubo_handle = make_buffer_handle(self->resources.size() - 1);
		rendergraph_add_uploadbufferpass_ex(self, u8"quick upload ubo", ubo_handle, resource.size, 0, data, nullptr, 0, nullptr);
		return ubo_handle;
	}
	texture_handle_t rendergraph_declare_texture_subresource(rendergraph_t* self, texture_handle_t parent_handle, uint8_t mipmap, uint8_t slice)
	{
		assert(rendergraph_texture_handle_valid(parent_handle));
		uint32_t parent = parent_handle.index;
		ResourceNode* textureNode = &self->resources[parent];
		assert(textureNode->resourceType == ResourceType::Texture);

		while (textureNode->parent != 0)
		{
			parent = textureNode->parent;
			textureNode = &self->resources[parent];
			assert(textureNode->resourceType == ResourceType::Texture);
		}

		self->resources.push_back(ResourceNode());
		auto& resourceNode = self->resources.back();
		resourceNode.resourceType = ResourceType::Texture;
		resourceNode.manageType = ManageType::SubResource;
		resourceNode.width = textureNode->width;
		resourceNode.height = textureNode->height;
		resourceNode.depth = textureNode->depth;
		resourceNode.format = textureNode->format;
		resourceNode.mipCount = textureNode->mipCount;
		resourceNode.arraySize = textureNode->arraySize;
		resourceNode.parent = parent;
		resourceNode.mipLevel = mipmap;
		resourceNode.arraySlice = slice;
		return texture_handle_t(self->resources.size() - 1);
	}
	uint32_t rendergraph_add_edge(rendergraph_t* self, index_type_t from, index_type_t to, ECGPUResourceState usage)
	{
		self->edges.emplace_back(from, to, usage);
		return self->edges.size() - 1;
	}
	ResourceNode::ResourceNode()
		: name(nullptr), resourceType(ResourceType::Texture), manageType(ManageType::Managed), width(0), height(0), depth(0), format(ECGPUFormat::CGPU_FORMAT_UNDEFINED), texture(nullptr), buffer(nullptr), holdOnLast(false), bufferType(CGPU_RESOURCE_TYPE_NONE), memoryUsage(CGPU_MEM_USAGE_UNKNOWN), size(0), mipCount(0), arraySize(0), parent(0), mipLevel(0), arraySlice(0)
	{
	}
	renderpass_builder_t::renderpass_builder_t(rendergraph_t* renderGraph, RenderPassNode* passNode, int passIndex)
		: renderGraph(renderGraph), passNode(passNode), passIndex(passIndex)
	{
	}
	RenderPassNode::RenderPassNode(const char8_t* name, pass_type type, std::pmr::memory_resource* const momory_resource)
		: name(name), writes(momory_resource), reads(momory_resource), type(type), passdata(nullptr), upload_buffer_context({ 0 })
	{
		if (type == PASS_TYPE_RENDER)
		{
			render_context = {};
		}
	}
	void renderpass_add_color_attachment(renderpass_builder_t* self, texture_handle_t texture, ECGPULoadAction load_action, uint32_t clearColor, ECGPUStoreAction store_action)
	{
		assert(self->passNode->type == PASS_TYPE_RENDER);
		assert(self->passNode->render_context.colorAttachmentCount <= self->passNode->render_context.colorAttachments.size());

		auto edge = rendergraph_add_edge(self->renderGraph, self->passIndex, texture.index, CGPU_RESOURCE_STATE_RENDER_TARGET);
		self->passNode->writes.push_back(edge);
		self->passNode->render_context.colorAttachments[self->passNode->render_context.colorAttachmentCount++] =
		{
			.clearColor = clearColor,
			.resourceIndex = texture.index,
			.load_action = load_action,
			.store_action = store_action,
			.valid = true,
		};
	}
	void renderpass_add_depth_attachment(renderpass_builder_t* self, texture_handle_t texture, ECGPULoadAction depth_load_action, float clearDepth, ECGPUStoreAction depth_store_action, ECGPULoadAction stencil_load_action, uint8_t clearStencil, ECGPUStoreAction stencil_store_action)
	{
		assert(self->passNode->type == PASS_TYPE_RENDER);
		assert(!self->passNode->render_context.depthAttachment.valid);

		auto edge1 = rendergraph_add_edge(self->renderGraph, texture.index, self->passIndex, CGPU_RESOURCE_STATE_UNDEFINED);
		self->passNode->reads.push_back(edge1);
		auto edge2 = rendergraph_add_edge(self->renderGraph, self->passIndex, texture.index, CGPU_RESOURCE_STATE_DEPTH_WRITE);
		self->passNode->writes.push_back(edge2);
		self->passNode->render_context.depthAttachment =
		{
			.clearDepth = clearDepth,
			.clearStencil = clearStencil,
			.resourceIndex = texture.index,
			.depth_load_action = depth_load_action,
			.depth_store_action = depth_store_action,
			.stencil_load_action = stencil_load_action,
			.stencil_store_action = stencil_store_action,
			.valid = true,
		};
	}
	void renderpass_sample(renderpass_builder_t* self, texture_handle_t texture)
	{
		auto edge = rendergraph_add_edge(self->renderGraph, texture.index, self->passIndex, CGPU_RESOURCE_STATE_SHADER_RESOURCE);
		self->passNode->reads.push_back(edge);
	}
	void renderpass_use_buffer(renderpass_builder_t* self, buffer_handle_t buffer)
	{
		assert(rendergraph_buffer_handle_valid(buffer));
		auto& resourceNode = self->renderGraph->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);

		ECGPUResourceState state = CGPU_RESOURCE_STATE_UNDEFINED;
		if (resourceNode.bufferType & CGPU_RESOURCE_TYPE_VERTEX_BUFFER)
			state = CGPU_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		else if (resourceNode.bufferType & CGPU_RESOURCE_TYPE_INDEX_BUFFER)
			state = CGPU_RESOURCE_STATE_INDEX_BUFFER;
		else if (resourceNode.bufferType & CGPU_RESOURCE_TYPE_UNIFORM_BUFFER)
			state = CGPU_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		assert(state != CGPU_RESOURCE_STATE_UNDEFINED);

		auto edge = rendergraph_add_edge(self->renderGraph, buffer.index, self->passIndex, state);
		self->passNode->reads.push_back(edge);
	}
	void renderpass_use_buffer_as(renderpass_builder_t* self, buffer_handle_t buffer, ECGPUResourceState state)
	{
		assert(rendergraph_buffer_handle_valid(buffer));
		auto& resourceNode = self->renderGraph->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);
		assert(state != CGPU_RESOURCE_STATE_UNDEFINED);

		auto edge = rendergraph_add_edge(self->renderGraph, buffer.index, self->passIndex, state);
		self->passNode->reads.push_back(edge);
	}
	void renderpass_set_executable(renderpass_builder_t* self, renderpass_executable executable, size_t passdata_size, void** passdata)
	{
		self->passNode->render_context.executable = executable;
		allocate_passdata(self->renderGraph, self->passNode, passdata_size, passdata);
	}
	void computepass_sample(renderpass_builder_t* self, texture_handle_t texture)
	{
		auto edge = rendergraph_add_edge(self->renderGraph, texture.index, self->passIndex, CGPU_RESOURCE_STATE_SHADER_RESOURCE);
		self->passNode->reads.push_back(edge);
	}
	void computepass_use_buffer(renderpass_builder_t* self, buffer_handle_t buffer)
	{
		assert(rendergraph_buffer_handle_valid(buffer));
		auto& resourceNode = self->renderGraph->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);

		ECGPUResourceState state = CGPU_RESOURCE_STATE_UNDEFINED;
		if (resourceNode.bufferType == CGPU_RESOURCE_TYPE_VERTEX_BUFFER)
			state = CGPU_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		else if (resourceNode.bufferType == CGPU_RESOURCE_TYPE_INDEX_BUFFER)
			state = CGPU_RESOURCE_STATE_INDEX_BUFFER;
		else if (resourceNode.bufferType == CGPU_RESOURCE_TYPE_UNIFORM_BUFFER)
			state = CGPU_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		else if (resourceNode.bufferType == CGPU_RESOURCE_TYPE_RW_BUFFER)
			state = CGPU_RESOURCE_STATE_UNORDERED_ACCESS;
		assert(state != CGPU_RESOURCE_STATE_UNDEFINED);

		auto edge = rendergraph_add_edge(self->renderGraph, buffer.index, self->passIndex, state);
		self->passNode->reads.push_back(edge);
	}
	void computepass_use_buffer_as(renderpass_builder_t* self, buffer_handle_t buffer, ECGPUResourceState state)
	{
		assert(rendergraph_buffer_handle_valid(buffer));
		auto& resourceNode = self->renderGraph->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);
		assert(state != CGPU_RESOURCE_STATE_UNDEFINED);

		auto edge = rendergraph_add_edge(self->renderGraph, buffer.index, self->passIndex, state);
		self->passNode->reads.push_back(edge);
	}
	void computepass_readwrite_texture(renderpass_builder_t* self, texture_handle_t texture)
	{
	}
	void computepass_readwrite_buffer(renderpass_builder_t* self, buffer_handle_t buffer)
	{
		assert(rendergraph_buffer_handle_valid(buffer));
		auto& resourceNode = self->renderGraph->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);

		auto edge = rendergraph_add_edge(self->renderGraph, buffer.index, self->passIndex, CGPU_RESOURCE_STATE_UNORDERED_ACCESS);
		self->passNode->reads.push_back(edge);
		auto edge2 = rendergraph_add_edge(self->renderGraph, self->passIndex, buffer.index, CGPU_RESOURCE_STATE_UNORDERED_ACCESS);
		self->passNode->writes.push_back(edge2);
	}
	void computepass_set_executable(renderpass_builder_t* self, renderpass_executable executable, size_t passdata_size, void** passdata)
	{
		self->passNode->compute_context.executable = executable;
		allocate_passdata(self->renderGraph, self->passNode, passdata_size, passdata);
	}
	void rg_texture_set_extent(rendergraph_t* self, texture_handle_t texture, uint32_t width, uint32_t height, uint32_t depth)
	{
		assert(is_valid_dynamic_texture_handle(self->resources, texture));
		auto& resourceNode = self->resources[texture.index];
		assert(resourceNode.resourceType == ResourceType::Texture);
		resourceNode.width = width;
		resourceNode.height = height;
		resourceNode.depth = depth;
	}
	void rg_texture_set_format(rendergraph_t* self, texture_handle_t texture, ECGPUFormat format)
	{
		assert(is_valid_dynamic_texture_handle(self->resources, texture));
		auto& resourceNode = self->resources[texture.index];
		assert(resourceNode.resourceType == ResourceType::Texture);
		resourceNode.format = format;
	}
	void rg_texture_set_depth_format(rendergraph_t* self, texture_handle_t texture, DepthBits depthBits, bool needStencil)
	{
		assert(is_valid_dynamic_texture_handle(self->resources, texture));
		auto FormatUtil_GetDepthStencilFormat = [](DepthBits depthBits, bool needStencil) -> ECGPUFormat
		{
			if (depthBits == DepthBits::D32 && needStencil)
				return CGPU_FORMAT_D32_SFLOAT_S8_UINT;
			else if (depthBits == DepthBits::D32 && !needStencil)
				return CGPU_FORMAT_D32_SFLOAT;
			else if (depthBits == DepthBits::D24 && needStencil)
				return CGPU_FORMAT_D24_UNORM_S8_UINT;
			else if (depthBits == DepthBits::D24 && !needStencil)
				return CGPU_FORMAT_X8_D24_UNORM;
			else if (depthBits == DepthBits::D16 && needStencil)
				return CGPU_FORMAT_D16_UNORM_S8_UINT;
			else if (depthBits == DepthBits::D16 && !needStencil)
				return CGPU_FORMAT_D16_UNORM;
			else
				return CGPU_FORMAT_UNDEFINED;
		};

		auto format = FormatUtil_GetDepthStencilFormat(depthBits, needStencil);
		auto& resourceNode = self->resources[texture.index];
		assert(resourceNode.resourceType == ResourceType::Texture);
		if (format != CGPU_FORMAT_UNDEFINED)
			resourceNode.format = format;
		else
			resourceNode.format = CGPU_FORMAT_UNDEFINED;
	}
	uint32_t rg_texture_get_width(rendergraph_t* self, texture_handle_t texture)
	{
		assert(is_valid_dynamic_texture_handle(self->resources, texture));
		auto& resourceNode = self->resources[texture.index];
		assert(resourceNode.resourceType == ResourceType::Texture);
		return resourceNode.width;
	}
	uint32_t rg_texture_get_height(rendergraph_t* self, texture_handle_t texture)
	{
		assert(is_valid_dynamic_texture_handle(self->resources, texture));
		auto& resourceNode = self->resources[texture.index];
		assert(resourceNode.resourceType == ResourceType::Texture);
		return resourceNode.height;
	}
	uint32_t rg_texture_get_depth(rendergraph_t* self, texture_handle_t texture)
	{
		assert(is_valid_dynamic_texture_handle(self->resources, texture));
		auto& resourceNode = self->resources[texture.index];
		assert(resourceNode.resourceType == ResourceType::Texture);
		return resourceNode.depth;
	}
	ECGPUFormat rg_texture_get_format(rendergraph_t* self, texture_handle_t texture)
	{
		assert(is_valid_dynamic_texture_handle(self->resources, texture));
		auto& resourceNode = self->resources[texture.index];
		assert(resourceNode.resourceType == ResourceType::Texture);
		return resourceNode.format;
	}
	void rg_buffer_set_size(rendergraph_t* self, buffer_handle_t buffer, uint32_t size)
	{
		assert(is_valid_dynamic_buffer_handle(self->resources, buffer));
		auto& resourceNode = self->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);

		auto nextPowerOfTwo = [](uint32_t n) -> uint32_t
			{
				if (n == 0)
				{
					return 1;
				}

				// Decrement n to handle the exact power of 2 case.
				n--;

				// Set all bits after the highest set bit
				n |= n >> 1;
				n |= n >> 2;
				n |= n >> 4;
				n |= n >> 8;
				n |= n >> 16;

				// Increment n to get next power of 2
				return n + 1;
			};

		resourceNode.size = nextPowerOfTwo(size);
	}
	void rg_buffer_set_type(rendergraph_t* self, buffer_handle_t buffer, ECGPUResourceType type)
	{
		assert(is_valid_dynamic_buffer_handle(self->resources, buffer));
		auto& resourceNode = self->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);
		resourceNode.bufferType = type;
	}
	void rg_buffer_set_usage(rendergraph_t* self, buffer_handle_t buffer, ECGPUMemoryUsage usage)
	{
		assert(is_valid_dynamic_buffer_handle(self->resources, buffer));
		auto& resourceNode = self->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);
		resourceNode.memoryUsage = usage;
	}
	void rg_buffer_import(rendergraph_t* self, buffer_handle_t buffer, Buffer* imported)
	{
		assert(is_valid_dynamic_buffer_handle(self->resources, buffer));
		auto& resourceNode = self->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);
		resourceNode.buffer = imported;
		resourceNode.manageType = ManageType::Imported;
		resourceNode.size = imported->handle->info->size;
		resourceNode.bufferType = imported->type;
		resourceNode.memoryUsage = (ECGPUMemoryUsage)imported->handle->info->memory_usage;
	}
	void rg_buffer_set_hold_on_last(rendergraph_t* self, buffer_handle_t buffer)
	{
		assert(is_valid_dynamic_buffer_handle(self->resources, buffer));
		auto& resourceNode = self->resources[buffer.index];
		assert(resourceNode.resourceType == ResourceType::Buffer);
		resourceNode.holdOnLast = true;
	}
}