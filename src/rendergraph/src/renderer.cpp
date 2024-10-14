#include "renderer.h"

#include <vector>
#include "hash.h"
#include "rendergraph.h"

namespace HGEGraphics
{
	Shader* create_shader(CGPUDeviceId device, const uint8_t* vert_data, uint32_t vert_length, const uint8_t* frag_data, uint32_t frag_length, const CGPUBlendStateDescriptor& blend_desc, const CGPUDepthStateDesc& depth_desc, const CGPURasterizerStateDescriptor& rasterizer_state)
	{
		CGPUShaderLibraryDescriptor vs_desc = {
			.name = u8"VertexShaderLibrary",
			.code = reinterpret_cast<const uint32_t*>(vert_data),
			.code_size = vert_length,
			.stage = CGPU_SHADER_STAGE_VERT,
		};
		CGPUShaderLibraryDescriptor ps_desc = {
			.name = u8"FragmentShaderLibrary",
			.code = reinterpret_cast<const uint32_t*>(frag_data),
			.code_size = (uint32_t)frag_length,
			.stage = CGPU_SHADER_STAGE_FRAG,
		};
		CGPUShaderLibraryId vertex_shader = cgpu_create_shader_library(device, &vs_desc);
		CGPUShaderLibraryId fragment_shader = cgpu_create_shader_library(device, &ps_desc);
		CGPUShaderEntryDescriptor ppl_shaders[2];
		ppl_shaders[0].stage = CGPU_SHADER_STAGE_VERT;
		ppl_shaders[0].entry = u8"main";
		ppl_shaders[0].library = vertex_shader;
		ppl_shaders[1].stage = CGPU_SHADER_STAGE_FRAG;
		ppl_shaders[1].entry = u8"main";
		ppl_shaders[1].library = fragment_shader;
		CGPURootSignatureDescriptor rs_desc = {
			.shaders = ppl_shaders,
			.shader_count = 2
		};
		auto root_sig = cgpu_create_root_signature(device, &rs_desc);

		auto shader = new Shader();
		shader->root_sig = root_sig;
		shader->vs = ppl_shaders[0];
		shader->ps = ppl_shaders[1];
		shader->blend_desc = blend_desc;
		shader->depth_desc = depth_desc;
		shader->rasterizer_state = rasterizer_state;
		return shader;
	}

	void free_shader(Shader* shader)
	{
		cgpu_free_root_signature(shader->root_sig);
		cgpu_free_shader_library(shader->vs.library);
		cgpu_free_shader_library(shader->ps.library);
		delete shader;
	}

	ComputeShader* create_compute_shader(CGPUDeviceId device, const uint8_t* comp_data, uint32_t comp_length)
	{
		CGPUShaderLibraryDescriptor cs_desc = {
			.name = u8"ComputeShaderLibrary",
			.code = reinterpret_cast<const uint32_t*>(comp_data),
			.code_size = (uint32_t)comp_length,
			.stage = CGPU_SHADER_STAGE_COMPUTE,
		};
		CGPUShaderLibraryId comp_shader = cgpu_create_shader_library(device, &cs_desc);

		CGPUShaderEntryDescriptor ppl_shaders[1];
		ppl_shaders[0].stage = CGPU_SHADER_STAGE_COMPUTE;
		ppl_shaders[0].entry = u8"main";
		ppl_shaders[0].library = comp_shader;
		CGPURootSignatureDescriptor rs_desc = {
			.shaders = ppl_shaders,
			.shader_count = 1
		};
		auto root_sig = cgpu_create_root_signature(device, &rs_desc);

		auto shader = new ComputeShader();
		shader->root_sig = root_sig;
		shader->cs = ppl_shaders[0];
		return shader;
	}

	void free_compute_shader(ComputeShader* shader)
	{
		cgpu_free_root_signature(shader->root_sig);
		cgpu_free_shader_library(shader->cs.library);
		delete shader;
	}

	Buffer* create_empty_buffer()
	{
		auto buffer = new Buffer();
		buffer->handle = CGPU_NULLPTR;
		buffer->type = CGPU_RESOURCE_TYPE_NONE;
		buffer->cur_state = CGPU_RESOURCE_STATE_UNDEFINED;
		buffer->dynamic_handle = {};
		return buffer;
	}

	Buffer* create_buffer(CGPUDeviceId device, const CGPUBufferDescriptor& desc)
	{
		auto buffer = create_empty_buffer();
		buffer->handle = cgpu_create_buffer(device, &desc);
		buffer->type = (ECGPUResourceType)desc.descriptors;
		return buffer;
	}

	void free_buffer(Buffer* buffer)
	{
		if (buffer->handle)
			cgpu_free_buffer(buffer->handle);
		delete buffer;
	}

	Mesh* create_empty_mesh()
	{
		auto mesh = new Mesh();
		mesh->vertex_layout = {};
		mesh->prim_topology = CGPU_PRIM_TOPO_POINT_LIST;
		mesh->vertices_count = 0;
		mesh->index_count = 0;
		mesh->index_stride = 0;
		mesh->vertex_buffer = nullptr;
		mesh->index_buffer = nullptr;
		mesh->prepared = false;
		return mesh;
	}

	void init_mesh(Mesh* mesh, CGPUDeviceId device, uint32_t vertex_count, uint32_t index_count, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout, uint32_t index_stride, bool update_vertex_data_from_compute_shader, bool update_index_data_from_compute_shader)
	{
		mesh->vertex_layout = vertex_layout;
		mesh->prim_topology = prim_topology;
		mesh->vertices_count = vertex_count;
		mesh->index_count = index_count;
		for (auto i = 0; i < vertex_layout.attribute_count; ++i)
		{
			mesh->vertex_stride += vertex_layout.attributes[i].elem_stride;
		}
		mesh->index_stride = index_stride;

		CGPUBufferDescriptor vertex_buffer_desc = {};
		vertex_buffer_desc.name = u8"vertex buffer";
		vertex_buffer_desc.flags = CGPU_BCF_PERSISTENT_MAP_BIT;
		vertex_buffer_desc.descriptors = update_vertex_data_from_compute_shader ? CGPU_RESOURCE_TYPE_VERTEX_BUFFER | CGPU_RESOURCE_TYPE_RW_BUFFER : CGPU_RESOURCE_TYPE_VERTEX_BUFFER;
		vertex_buffer_desc.memory_usage = CGPU_MEM_USAGE_GPU_ONLY;
		vertex_buffer_desc.size = vertex_count * mesh->vertex_stride;
		mesh->vertex_buffer = create_buffer(device, vertex_buffer_desc);

		if (index_count > 0)
		{
			CGPUBufferDescriptor index_buffer_desc = {};
			index_buffer_desc.name = u8"index buffer";
			index_buffer_desc.flags = CGPU_BCF_PERSISTENT_MAP_BIT;
			index_buffer_desc.descriptors = update_index_data_from_compute_shader ? CGPU_RESOURCE_TYPE_INDEX_BUFFER | CGPU_RESOURCE_TYPE_RW_BUFFER : CGPU_RESOURCE_TYPE_INDEX_BUFFER;
			index_buffer_desc.memory_usage = CGPU_MEM_USAGE_GPU_ONLY;
			index_buffer_desc.size = index_count * mesh->index_stride;
			mesh->index_buffer = create_buffer(device, index_buffer_desc);
		}
		mesh->prepared = false;
	}

	Mesh* create_mesh(CGPUDeviceId device, uint32_t vertex_count, uint32_t index_count, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout, uint32_t index_stride, bool update_vertex_data_from_compute_shader, bool update_index_data_from_compute_shader)
	{
		auto mesh = create_empty_mesh();
		init_mesh(mesh, device, vertex_count, index_count, prim_topology, vertex_layout, index_stride, update_vertex_data_from_compute_shader, update_index_data_from_compute_shader);
		return mesh;
	}

	Mesh* create_dynamic_mesh(ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout, uint32_t index_stride)
	{
		auto mesh = create_empty_mesh();
		mesh->vertex_layout = vertex_layout;
		mesh->prim_topology = prim_topology;
		mesh->vertices_count = 0;
		mesh->vertex_stride = 0;
		for (auto i = 0; i < vertex_layout.attribute_count; ++i)
		{
			mesh->vertex_stride += vertex_layout.attributes[i].elem_stride;
		}
		mesh->index_stride = index_stride;
		mesh->vertex_buffer = create_empty_buffer();
		mesh->index_buffer = create_empty_buffer();
		mesh->prepared = true;
		return mesh;
	}

	buffer_handle_t declare_dynamic_vertex_buffer(Mesh* mesh, rendergraph_t* rg, uint32_t count)
	{
		auto dynamic_vertex_buffer = rendergraph_import_dynamic_buffer(rg, mesh->vertex_buffer);
		rg_buffer_set_size(rg, dynamic_vertex_buffer, count * mesh->vertex_stride);
		rg_buffer_set_type(rg, dynamic_vertex_buffer, CGPU_RESOURCE_TYPE_VERTEX_BUFFER);
		rg_buffer_set_usage(rg, dynamic_vertex_buffer, CGPU_MEM_USAGE_GPU_ONLY);
		mesh->vertex_buffer->dynamic_handle = dynamic_vertex_buffer;
		mesh->vertices_count = count;
		return mesh->vertex_buffer->dynamic_handle;
	}

	buffer_handle_t declare_dynamic_index_buffer(Mesh* mesh, rendergraph_t* rg, uint32_t count)
	{
		auto dynamic_index_buffer = rendergraph_import_dynamic_buffer(rg, mesh->index_buffer);
		rg_buffer_set_size(rg, dynamic_index_buffer, count * mesh->index_stride);
		rg_buffer_set_type(rg, dynamic_index_buffer, CGPU_RESOURCE_TYPE_INDEX_BUFFER);
		rg_buffer_set_usage(rg, dynamic_index_buffer, CGPU_MEM_USAGE_GPU_ONLY);
		mesh->index_buffer->dynamic_handle = dynamic_index_buffer;
		mesh->index_count = count;
		return mesh->index_buffer->dynamic_handle;
	}

	void dynamic_mesh_reset(Mesh* mesh)
	{
		mesh->vertices_count = 0;
		mesh->index_count = 0;
		mesh->vertex_buffer->dynamic_handle = {};
		mesh->index_buffer->dynamic_handle = {};
	}

	void free_mesh(Mesh* mesh)
	{
		if (mesh->vertex_buffer)
			free_buffer(mesh->vertex_buffer);
		if (mesh->index_buffer)
			free_buffer(mesh->index_buffer);
		delete mesh;
	}

	Texture* create_empty_texture()
	{
		auto texture = new Texture();
		texture->handle = CGPU_NULLPTR;
		texture->view = CGPU_NULLPTR;
		texture->cur_states.clear();
		texture->states_consistent = false;
		texture->prepared = false;
		texture->dynamic_handle = {};
		return texture;
	}

	void init_texture(Texture* texture, CGPUDeviceId device, const CGPUTextureDescriptor& desc)
	{
		CGPUTextureDescriptor new_desc = desc;
		if (desc.depth == 1 && desc.height == 1)
			new_desc.flags |= CGPU_TCF_FORCE_2D;

		texture->handle = cgpu_create_texture(device, &new_desc);
		texture->cur_states.resize(new_desc.array_size * new_desc.mip_levels);
		std::fill(texture->cur_states.begin(), texture->cur_states.end(), CGPU_RESOURCE_STATE_UNDEFINED);
		texture->states_consistent = true;

		uint32_t arrayCount = texture->handle->info->array_size_minus_one + 1;
		ECGPUTextureDimension dims = CGPU_TEX_DIMENSION_2D;
		if (CGPU_RESOURCE_TYPE_TEXTURE_CUBE == (new_desc.descriptors & CGPU_RESOURCE_TYPE_TEXTURE_CUBE))
			dims = CGPU_TEX_DIMENSION_CUBE;
		else if (new_desc.depth > 1)
			dims = CGPU_TEX_DIMENSION_3D;
		CGPUTextureViewDescriptor view_desc;
		view_desc.texture = texture->handle;
		view_desc.format = texture->handle->info->format;
		view_desc.usages = CGPU_TVU_SRV;
		view_desc.aspects = CGPU_TVA_COLOR;
		view_desc.dims = dims;
		view_desc.base_array_layer = 0;
		view_desc.array_layer_count = arrayCount;
		view_desc.base_mip_level = 0;
		view_desc.mip_level_count = texture->handle->info->mip_levels;
		texture->view = cgpu_create_texture_view(device, &view_desc);
		texture->prepared = false;
	}

	Texture* create_texture(CGPUDeviceId device, const CGPUTextureDescriptor& desc)
	{
		auto texture = create_empty_texture();
		init_texture(texture, device, desc);
		return texture;
	}

	void free_texture(Texture* texture)
	{
		if (texture->view)
			cgpu_free_texture_view(texture->view);
		if (texture->handle)
			cgpu_free_texture(texture->handle);
		delete texture;
	}

	void init_backbuffer(Backbuffer* backbuffer, CGPUSwapChainId swapchain, int index)
	{
		backbuffer->texture.handle = swapchain->back_buffers[index];
		backbuffer->texture.view = CGPU_NULLPTR;
		backbuffer->texture.cur_states.resize(1);
		backbuffer->texture.cur_states[0] = CGPU_RESOURCE_STATE_UNDEFINED;
		backbuffer->texture.states_consistent = true;
		backbuffer->texture.dynamic_handle = {};
	}

	void free_backbuffer(Backbuffer* backbuffer)
	{
		backbuffer->texture.handle = CGPU_NULLPTR;
		backbuffer->texture.view = CGPU_NULLPTR;
		backbuffer->texture.cur_states.clear();
		backbuffer->texture.states_consistent = false;
	}

	void push_constants(RenderPassEncoder* encoder, Shader* shader, const char8_t* name, const void* data)
	{
		cgpu_render_encoder_push_constants(encoder->encoder, shader->root_sig, name, data);
	}

	void update_render_pipeline(RenderPassEncoder* encoder, Shader* shader, ECGPUPrimitiveTopology mesh_topology, const CGPUVertexLayout& vertex_layout)
	{
		auto pipeline = encoder->context->pipelinePool.getGraphicsPipeline(encoder, shader, mesh_topology, vertex_layout);
		if (pipeline && pipeline->handle != encoder->last_render_pipeline)
		{
			cgpu_render_encoder_bind_pipeline(encoder->encoder, pipeline->handle);
			if (encoder->context->pipelinePool.dynamicStateT1Enabled())
			{
				cgpu_raster_state_encoder_set_cull_mode(encoder->raster_state_encoder, shader->rasterizer_state.cull_mode);
				cgpu_raster_state_encoder_set_front_face(encoder->raster_state_encoder, shader->rasterizer_state.front_face);
				cgpu_raster_state_encoder_set_primitive_topology(encoder->raster_state_encoder, mesh_topology);
				cgpu_raster_state_encoder_set_depth_test_enabled(encoder->raster_state_encoder, shader->depth_desc.depth_test);
				cgpu_raster_state_encoder_set_depth_write_enabled(encoder->raster_state_encoder, shader->depth_desc.depth_write);
				cgpu_raster_state_encoder_set_depth_compare_op(encoder->raster_state_encoder, shader->depth_desc.depth_func);
			}
			encoder->last_render_pipeline = pipeline->handle;
			memset(encoder->last_bind_resources, 0, sizeof(encoder->last_bind_resources));
		}
	}

	void update_descriptor_set(RenderPassEncoder* encoder, CGPURootSignatureId root_sig, bool is_graphics)
	{
		for (uint32_t i = 0; i < std::min(4u, root_sig->table_count); ++i)
		{
			auto& table = root_sig->tables[i];
			CGPUDescriptorSetDescriptor dset_desc =
			{
				.root_signature = root_sig,
				.set_index = table.set_index,
			};
			
			auto dset = encoder->context->descriptorSetPool.getDescriptorSet(dset_desc);
			encoder->context->allocated_dsets.push_back(dset);

			const uint32_t data_size = 64;
			CGPUDescriptorData datas[data_size] = { 0 };
			uint32_t data_count = 0;
			uint32_t texture_view_count = 0;
			uint32_t sampler_count = 0;
			uint32_t buffer_count = 0;
			uint32_t offset_size_count = 0;
			for (uint32_t j = 0; j < std::min(data_size, table.resources_count); ++j)
			{
				auto& res = table.resources[j];
				CGPUDescriptorData data =
				{
					.binding = res.binding,
					.binding_type = res.type,
					.count = 1,
				};
				if (res.type == CGPU_RESOURCE_TYPE_TEXTURE)
				{
					CGPUTextureViewId textureview = CGPU_NULLPTR;
					for (auto iter = encoder->context->global_texture_table.rbegin(); iter != encoder->context->global_texture_table.rend(); ++iter)
					{
						auto& binder = *iter;
						if (binder.set == i && binder.bind == res.binding)
						{
							if (rendergraph_texture_handle_valid(binder.texture_handle))
								textureview = rendergraph_resolve_texture_view(encoder, binder.texture_handle);
							else if (binder.texture && binder.texture->prepared)
								textureview = binder.texture->view;
							break;
						}
					}
					if (!textureview)
						textureview = encoder->context->default_texture;
					encoder->textureviews[texture_view_count] = textureview;
					data.textures = encoder->textureviews + texture_view_count;
					++texture_view_count;
				}
				else if (res.type == CGPU_RESOURCE_TYPE_SAMPLER)
				{
					CGPUSamplerId sampler = CGPU_NULLPTR;
					for (auto iter = encoder->context->global_sampler_table.rbegin(); iter != encoder->context->global_sampler_table.rend(); ++iter)
					{
						auto& binder = *iter;
						if (binder.set == i && binder.bind == res.binding)
						{
							sampler = binder.sampler;
							break;
						}
					}
					if (!sampler)
						;	// TODO
					encoder->samplers[sampler_count] = sampler;
					data.samplers = encoder->samplers + sampler_count;
					++sampler_count;
				}
				else if (res.type == CGPU_RESOURCE_TYPE_UNIFORM_BUFFER || res.type == CGPU_RESOURCE_TYPE_RW_BUFFER)
				{
					for (auto iter = encoder->context->global_buffer_table.rbegin(); iter != encoder->context->global_buffer_table.rend(); ++iter)
					{
						auto& binder = *iter;
						if (binder.set == i && binder.bind == res.binding)
						{
							encoder->buffers[buffer_count] = rendergraph_resolve_buffer(encoder, binder.buffer);
							if (binder.offset != 0 || binder.size != 0)
							{
								encoder->buffer_offset_sizes[offset_size_count] = binder.offset;
								data.buffers_params.offsets = encoder->buffer_offset_sizes + (offset_size_count++);
								encoder->buffer_offset_sizes[offset_size_count] = binder.size;
								data.buffers_params.sizes = encoder->buffer_offset_sizes + (offset_size_count++);
							}
							data.buffers = encoder->buffers + buffer_count;
							++buffer_count;
							break;
						}
					}
				}
				if (data.ptrs != nullptr)
					datas[data_count++] = data;
			}

			if (data_count > 0)
			{
				bool dset_dirty = memcmp(datas, encoder->last_bind_resources[i], sizeof(CGPUDescriptorData) * data_count) != 0;
				bool offset_size_dirty = memcmp(encoder->buffer_offset_sizes, encoder->last_buffer_offset_sizes[i], sizeof(float) * 2 * offset_size_count);
				if (dset_dirty || offset_size_dirty)
				{
					cgpu_update_descriptor_set(dset->handle, datas, data_count);
					if (is_graphics)
						cgpu_render_encoder_bind_descriptor_set(encoder->encoder, dset->handle);
					else
						cgpu_compute_encoder_bind_descriptor_set(encoder->compute_encoder, dset->handle);
					memcpy(encoder->last_bind_resources[i], datas, sizeof(CGPUDescriptorData) * data_count);
					memcpy(encoder->last_buffer_offset_sizes[i], encoder->buffer_offset_sizes, sizeof(float) * 2 * offset_size_count);
				}
			}
		}
	}

	void update_mesh(RenderPassEncoder* encoder, Mesh* mesh)
	{
		CGPUBufferId vertex_buffer = CGPU_NULLPTR;
		if (rendergraph_buffer_handle_valid(mesh->vertex_buffer->dynamic_handle))
		{
			auto vertex_buffer_handle = mesh->vertex_buffer->dynamic_handle;
			vertex_buffer = rendergraph_resolve_buffer(encoder, vertex_buffer_handle);
		}
		else if (mesh->vertex_buffer)
		{
			vertex_buffer = mesh->vertex_buffer->handle;
		}
		const uint32_t vert_stride = mesh->vertex_stride;
		if (encoder->last_vertex_buffer != vertex_buffer || encoder->last_vertex_buffer_stride != vert_stride)
		{
			if (vertex_buffer)
				cgpu_render_encoder_bind_vertex_buffers(encoder->encoder, 1, &vertex_buffer, &vert_stride, nullptr);
			encoder->last_vertex_buffer = vertex_buffer;
			encoder->last_vertex_buffer_stride = vert_stride;
		}

		CGPUBufferId index_buffer = CGPU_NULLPTR;
		if (mesh->index_buffer)
		{
			if (rendergraph_buffer_handle_valid(mesh->index_buffer->dynamic_handle))
			{
				auto index_buffer_handle = mesh->index_buffer->dynamic_handle;
				index_buffer = rendergraph_resolve_buffer(encoder, index_buffer_handle);
			}
			else
			{
				index_buffer = mesh->index_buffer->handle;
			}
		}
		const uint32_t index_stride = mesh->index_stride;
		if (encoder->last_index_buffer != index_buffer || encoder->last_index_buffer_stride != index_stride)
		{
			if (index_buffer)
				cgpu_render_encoder_bind_index_buffer(encoder->encoder, index_buffer, index_stride, 0);
			encoder->last_index_buffer = index_buffer;
			encoder->last_index_buffer_stride = index_stride;
		}
	}

	void draw(RenderPassEncoder* encoder, Shader* shader, Mesh* mesh)
	{
		if (!mesh->prepared)
			return;
		update_render_pipeline(encoder, shader, mesh->prim_topology, mesh->vertex_layout);
		update_descriptor_set(encoder, shader->root_sig, true);
		update_mesh(encoder, mesh);
		if (encoder->last_index_buffer)
			cgpu_render_encoder_draw_indexed(encoder->encoder, mesh->index_count, 0, 0);
		else
			cgpu_render_encoder_draw(encoder->encoder, mesh->vertices_count, 0);
	}

	void draw_submesh(RenderPassEncoder* encoder, Shader* shader, Mesh* mesh, uint32_t index_count, uint32_t first_index, uint32_t vertex_count, uint32_t first_vertex)
	{
		if (!mesh->prepared)
			return;
		update_render_pipeline(encoder, shader, mesh->prim_topology, mesh->vertex_layout);
		update_descriptor_set(encoder, shader->root_sig, true);
		update_mesh(encoder, mesh);
		if (encoder->last_index_buffer)
			cgpu_render_encoder_draw_indexed(encoder->encoder, index_count, first_index, first_vertex);
		else
			cgpu_render_encoder_draw(encoder->encoder, vertex_count, first_vertex);
	}

	static CGPUVertexLayout procedure_vertex_layout = { .attribute_count = 0 };
	void draw_procedure(RenderPassEncoder* encoder, Shader* shader, ECGPUPrimitiveTopology mesh_topology, uint32_t vertex_count)
	{
		update_render_pipeline(encoder, shader, mesh_topology, procedure_vertex_layout);
		update_descriptor_set(encoder, shader->root_sig, true);
		cgpu_render_encoder_draw(encoder->encoder, vertex_count, 0);
	}

	void update_compute_pipeline(RenderPassEncoder* encoder, ComputeShader* shader)
	{
		auto pipeline = encoder->context->computePipelinePool.getComputePipeline(shader);
		if (pipeline && pipeline->handle != encoder->last_compute_pipeline)
		{
			cgpu_compute_encoder_bind_pipeline(encoder->compute_encoder, pipeline->handle);
			encoder->last_compute_pipeline = pipeline->handle;
			memset(encoder->last_bind_resources, 0, sizeof(encoder->last_bind_resources));
		}
	}

	void dispatch(RenderPassEncoder* encoder, ComputeShader* shader, uint32_t thread_x, uint32_t thread_y, uint32_t thread_z)
	{
		update_compute_pipeline(encoder, shader);
		update_descriptor_set(encoder, shader->root_sig, false);
		cgpu_compute_encoder_dispatch(encoder->compute_encoder, thread_x, thread_y, thread_z);
	}

	void set_global_texture(RenderPassEncoder* encoder, Texture* texture, int set, int slot)
	{
		encoder->context->global_texture_table.push_back({ texture, {}, set, slot });
	}

	void set_global_texture_handle(RenderPassEncoder* encoder, texture_handle_t texture, int set, int slot)
	{
		encoder->context->global_texture_table.push_back({ nullptr, texture, set, slot });
	}

	void set_global_sampler(RenderPassEncoder* encoder, CGPUSamplerId sampler, int set, int slot)
	{
		encoder->context->global_sampler_table.push_back({ sampler, set, slot });
	}

	void set_global_buffer(RenderPassEncoder* encoder, buffer_handle_t buffer, int set, int slot)
	{
		encoder->context->global_buffer_table.push_back({ buffer, set, slot, 0, 0 });
	}

	void set_global_buffer_with_offset_size(RenderPassEncoder* encoder, buffer_handle_t buffer, int set, int slot, uint64_t offset, uint64_t size)
	{
		encoder->context->global_buffer_table.push_back({ buffer, set, slot, offset, size });
	}

	void upload(UploadEncoder* encoder, uint64_t offset, uint64_t length, void* data)
	{
		char* address = (char*)encoder->address + offset;
		memcpy(address, data, length);
	}

	ExecutorContext::ExecutorContext(CGPUDeviceId device, CGPUQueueId gfx_queue, bool profile, std::pmr::memory_resource* memory_resource)
		: device(device), memory_resource(memory_resource), renderPassPool(device, memory_resource), framebufferPool(device, memory_resource), texturePool(device, gfx_queue, nullptr, memory_resource), pipelinePool(device, nullptr, memory_resource), computePipelinePool(device, nullptr, memory_resource), textureViewPool(nullptr, memory_resource), bufferPool(device, nullptr, memory_resource), descriptorSetPool(device, memory_resource), allocated_dsets(memory_resource)
		, cmds(memory_resource), allocated_cmds(memory_resource), global_texture_table(memory_resource), global_sampler_table(memory_resource), global_buffer_table(memory_resource)
	{
		cmdPool = cgpu_create_command_pool(gfx_queue, CGPU_NULLPTR);
		if (profile)
			profiler = new Profiler(device, gfx_queue, memory_resource);
		auto adapter_detail = cgpu_query_adapter_detail(device->adapter);
		support_shading_rate = adapter_detail->support_shading_rate;
	}

	void ExecutorContext::newFrame()
	{
		++timestamp;

		cgpu_reset_command_pool(cmdPool);

		for (auto cmd : allocated_cmds)
			cmds.push_back(cmd);
		allocated_cmds.clear();

		global_texture_table.clear();
		global_sampler_table.clear();
		global_buffer_table.clear();

		framebufferPool.newFrame();
		descriptorSetPool.newFrame();
		textureViewPool.newFrame();
		bufferPool.newFrame();
		pipelinePool.newFrame();
		computePipelinePool.newFrame();
		renderPassPool.newFrame();
		texturePool.newFrame();

		for (auto& dset : allocated_dsets)
			descriptorSetPool.releaseResource(dset);
		allocated_dsets.clear();
	}

	CGPUCommandBufferId ExecutorContext::requestCmd()
	{
		CGPUCommandBufferId cmd;
		if (!cmds.empty())
		{
			cmd = cmds.back();
			cmds.pop_back();
		}
		else
		{
			CGPUCommandBufferDescriptor cmd_desc = { .is_secondary = false };
			cmd = cgpu_create_command_buffer(cmdPool, &cmd_desc);
		}

		allocated_cmds.push_back(cmd);
		return cmd;
	}

	void ExecutorContext::destroy()
	{
		delete profiler;
		profiler = nullptr;
		framebufferPool.destroy();
		textureViewPool.destroy();
		pipelinePool.destroy();
		computePipelinePool.destroy();
		renderPassPool.destroy();
		texturePool.destroy();
		bufferPool.destroy();
		for (auto cmd : cmds)
		{
			cgpu_free_command_buffer(cmd);
		}
		cmds.clear();
		for (auto cmd : allocated_cmds)
		{
			cgpu_free_command_buffer(cmd);
		}
		allocated_cmds.clear();
		if (cmdPool)
			cgpu_free_command_pool(cmdPool);
		cmdPool = CGPU_NULLPTR;
		global_texture_table.clear();
		global_sampler_table.clear();
		global_buffer_table.clear();
		device = CGPU_NULLPTR;
	}
	void ExecutorContext::pre_destroy()
	{
		for (auto& dset : allocated_dsets)
		{
			descriptorSetPool.releaseResource(dset);
		}
		allocated_dsets.clear();
		descriptorSetPool.destroy();
	}
}