#include "rendergraph_executor.h"

#include "renderer.h"
#include <cassert>

namespace HGEGraphics
{
	struct RuntimePass
	{
		CompiledRenderPassNode* passNode;
		RenderPass* renderPass;
		Framebuffer* framebuffer;
	};

	Texture* getTexture(std::pmr::vector<CompiledResourceNode>& resources, CompiledResourceNode& resource)
	{
		if (resource.manageType == ManageType::Managed)
			return resource.managered_texture->texture;
		else if (resource.manageType == ManageType::Imported)
			return resource.imported_texture;
		else
		{
			auto& parentResource = resources[resource.parent];
			assert(parentResource.parent == 0 && parentResource.resourceType == ResourceType::Texture);
			return parentResource.manageType == ManageType::Managed ? parentResource.managered_texture->texture : parentResource.imported_texture;
		}
	}

	void place_barriers(ExecutorContext& context, CompiledRenderGraph& compiledRenderGraph, const CompiledRenderPassNode& pass, RuntimePass& runtime, CGPUCommandBufferId cmd)
	{
		uint32_t texture_barrier_count = 0;
		uint32_t buffer_barrier_count = 0;
		const size_t length = 16;
		CGPUTextureBarrier texture_barriers[length];
		CGPUBufferBarrier buffer_barriers[length];
		auto place_texture_barriers_impl = [&](decltype(compiledRenderGraph.resources)& resources, const decltype(pass.reads)& edges, CGPUCommandBufferId cmd)
		{
			auto add_barrier = [](uint32_t& buffer_barrier_count, CGPUBufferBarrier buffer_barriers[], uint32_t& texture_barrier_count, CGPUTextureBarrier texture_barriers[], CGPUCommandBufferId cmd)
			{
				if (texture_barrier_count >= length || buffer_barrier_count >= length)
				{
					CGPUResourceBarrierDescriptor barrier_desc = { .buffer_barriers = buffer_barriers, .buffer_barriers_count = buffer_barrier_count, .texture_barriers = texture_barriers, .texture_barriers_count = texture_barrier_count, };
					cgpu_cmd_resource_barrier(cmd, &barrier_desc);
					texture_barrier_count = 0;
					buffer_barrier_count = 0;
				}
			};

			for (auto& edge : edges)
			{
				auto& resource = resources[edge.index];
				if (resource.resourceType == ResourceType::Texture && edge.usage != CGPU_RESOURCE_STATE_UNDEFINED)
				{
					auto texture = getTexture(resources, resource);
					auto force_barrier = edge.usage == CGPU_RESOURCE_STATE_RENDER_TARGET || edge.usage == CGPU_RESOURCE_STATE_DEPTH_WRITE || edge.usage == CGPU_RESOURCE_STATE_COPY_DEST;
					if (resource.manageType != ManageType::SubResource)
					{
						if (texture->states_consistent == true || texture->cur_states.size() == 1)
						{
							auto cur_state = texture->cur_states[0];
							if (cur_state != edge.usage || force_barrier)
							{
								texture_barriers[texture_barrier_count++] = {
									.texture = texture->handle,
									.src_state = cur_state,
									.dst_state = edge.usage,
									.subresource_barrier = 0,
									.mip_level = 0,
									.array_layer = 0,
								};
								add_barrier(buffer_barrier_count, buffer_barriers, texture_barrier_count, texture_barriers, cmd);
								for (auto& state : texture->cur_states)
									state = edge.usage;
							}
						}
						else
						{
							for (size_t i = 0; i < texture->cur_states.size(); ++i)
							{
								auto& cur_state = texture->cur_states[i];
								if (cur_state != edge.usage || force_barrier)
								{
									texture_barriers[texture_barrier_count++] = {
										.texture = texture->handle,
										.src_state = cur_state,
										.dst_state = edge.usage,
										.subresource_barrier = 1,
										.mip_level = uint8_t(i % resource.mipCount),
										.array_layer = uint8_t(i / resource.mipCount),
									};
									add_barrier(buffer_barrier_count, buffer_barriers, texture_barrier_count, texture_barriers, cmd);
									cur_state = edge.usage;
								}
							}
						}
						texture->states_consistent = true;
					}
					else
					{
						auto& cur_state = texture->cur_states[resource.mipLevel + resource.arraySlice * resource.mipCount];
						if (cur_state != edge.usage || force_barrier)
						{
							texture_barriers[texture_barrier_count++] = {
								.texture = texture->handle,
								.src_state = cur_state,
								.dst_state = edge.usage,
								.subresource_barrier = 1,
								.mip_level = resource.mipLevel,
								.array_layer = resource.arraySlice,
							};
							add_barrier(buffer_barrier_count, buffer_barriers, texture_barrier_count, texture_barriers, cmd);
							cur_state = edge.usage;
							texture->states_consistent = false;
						}
					}
				}
				else if (resource.resourceType == ResourceType::Buffer && edge.usage != CGPU_RESOURCE_STATE_UNDEFINED)
				{
					auto buffer = resource.manageType == ManageType::Managed ? resource.managed_buffer->handle : resource.imported_buffer->handle;
					auto cur_state = resource.manageType == ManageType::Managed ? resource.managed_buffer->cur_state : resource.imported_buffer->cur_state;
					auto force_barrier = edge.usage == CGPU_RESOURCE_STATE_COPY_DEST;
					if (cur_state != edge.usage || force_barrier)
					{
						buffer_barriers[buffer_barrier_count++] = {
							.buffer = buffer,
							.src_state = cur_state,
							.dst_state = edge.usage,
						};
						add_barrier(buffer_barrier_count, buffer_barriers, texture_barrier_count, texture_barriers, cmd);
						if (resource.manageType == ManageType::Managed)
							resource.managed_buffer->cur_state = edge.usage;
						else
							resource.imported_buffer->cur_state = edge.usage;
					}
				}
			}
		};

		place_texture_barriers_impl(compiledRenderGraph.resources, pass.reads, cmd);
		place_texture_barriers_impl(compiledRenderGraph.resources, pass.writes, cmd);

		if (texture_barrier_count > 0 || buffer_barrier_count > 0)
		{
			CGPUResourceBarrierDescriptor barrier_desc = { .buffer_barriers = buffer_barriers, .buffer_barriers_count = buffer_barrier_count, .texture_barriers = texture_barriers, .texture_barriers_count = texture_barrier_count, };
			cgpu_cmd_resource_barrier(cmd, &barrier_desc);
			texture_barrier_count = 0;
			buffer_barrier_count = 0;
		}
	}

	void execute_render_pass(ExecutorContext& context, CompiledRenderGraph& compiledRenderGraph, const CompiledRenderPassNode& pass, RuntimePass& runtime, CGPUCommandBufferId cmd)
	{
		int attachment_count = pass.colorAttachmentCount + (pass.depthAttachment.valid ? 1 : 0);
		if (attachment_count > 0)
		{
			CGPURenderPassDescriptor rpDesc = {};
			rpDesc.sample_count = ECGPUSampleCount::CGPU_SAMPLE_COUNT_1;
			for (size_t i = 0; i < pass.colorAttachmentCount; ++i)
			{
				rpDesc.color_attachments[i] =
				{
					.format = compiledRenderGraph.resources[pass.colorAttachments[i].resourceIndex].format,
					.load_action = pass.colorAttachments[i].load_action,
					.store_action = pass.colorAttachments[i].store_action,
				};
			}

			if (pass.depthAttachment.valid)
			{
				rpDesc.depth_stencil =
				{
					.format = compiledRenderGraph.resources[pass.depthAttachment.resourceIndex].format,
					.depth_load_action = pass.depthAttachment.depth_load_action,
					.depth_store_action = pass.depthAttachment.depth_store_action,
					.stencil_load_action = pass.depthAttachment.stencil_load_action,
					.stencil_store_action = pass.depthAttachment.stencil_store_action,
				};
			}

			runtime.renderPass = context.renderPassPool.getRenderPass(rpDesc);
			CGPUFramebufferDescriptor fbDesc = {};
			fbDesc.renderpass = runtime.renderPass->renderPass;
			fbDesc.attachment_count = pass.colorAttachmentCount + (pass.depthAttachment.valid ? 1 : 0);
			for (int i = 0; i < pass.colorAttachmentCount; ++i)
			{
				auto& resource = compiledRenderGraph.resources[pass.colorAttachments[i].resourceIndex];
				auto texture = getTexture(compiledRenderGraph.resources, resource);
				assert(texture->handle->info->depth == 1);
				CGPUTextureViewDescriptor desc = {};
				desc.texture = texture->handle;
				desc.format = texture->handle->info->format;
				desc.usages = CGPU_TVU_RTV_DSV;
				desc.aspects = CGPU_TVA_COLOR;
				desc.dims = CGPU_TEX_DIMENSION_2D;
				desc.base_array_layer = 0;
				desc.array_layer_count = 1;
				desc.base_mip_level = resource.mipLevel;
				desc.mip_level_count = 1;
				auto textureView = context.textureViewPool.getResource(desc);
				fbDesc.attachments[i] = textureView->handle;
			}
			if (pass.depthAttachment.valid)
			{
				auto& resource = compiledRenderGraph.resources[pass.depthAttachment.resourceIndex];
				auto texture = getTexture(compiledRenderGraph.resources, resource);
				assert(texture->handle->info->depth == 1);
				CGPUTextureViewDescriptor desc = {};
				desc.texture = texture->handle;
				desc.format = texture->handle->info->format;
				desc.usages = CGPU_TVU_RTV_DSV;
				desc.aspects = CGPU_TVA_DEPTH | CGPU_TVA_STENCIL;
				desc.dims = CGPU_TEX_DIMENSION_2D;
				desc.base_array_layer = 0;
				desc.array_layer_count = 1;
				desc.base_mip_level = resource.mipLevel;
				desc.mip_level_count = 1;
				auto textureView = context.textureViewPool.getResource(desc);
				fbDesc.attachments[pass.colorAttachmentCount] = textureView->handle;
			}

			auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max<uint64_t>(size >> mip, 1ull); };
			fbDesc.width = mipedSize(fbDesc.attachments[0]->info.texture->info->width, fbDesc.attachments[0]->info.base_mip_level);
			fbDesc.height = mipedSize(fbDesc.attachments[0]->info.texture->info->height, fbDesc.attachments[0]->info.base_mip_level);
			fbDesc.layers = 1;
			runtime.framebuffer = context.framebufferPool.getFramebuffer(fbDesc);

			CGPUClearValue clear_values[9];

			uint32_t clear_value_count = 0;
			for (auto i = 0; i < pass.colorAttachmentCount; ++i)
			{
				if (pass.colorAttachments[i].load_action == ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR)
				{
					float red = ((pass.colorAttachments[i].clearColor & 0xff) >> 0) / 255.0f;
					float green = ((pass.colorAttachments[i].clearColor & 0xff00) >> 8) / 255.0f;
					float blue = ((pass.colorAttachments[i].clearColor & 0xff0000) >> 16) / 255.0f;
					float alpha = ((pass.colorAttachments[i].clearColor & 0xff000000) >> 24) / 255.0f;

					CGPUClearValue clear_color = {
						.color = { red, green, blue, alpha },
						.is_color = true,
					};
					clear_values[clear_value_count++] = clear_color;
				}
			}

			if (pass.depthAttachment.valid)
			{
				if (pass.depthAttachment.depth_load_action == ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR || pass.depthAttachment.stencil_load_action == ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR)
				{
					CGPUClearValue clear_color = {
						.depth = pass.depthAttachment.clearDepth,
						.stencil = pass.depthAttachment.clearStencil,
						.is_color = false,
					};
					clear_values[clear_value_count++] = clear_color;
				}
			}

			CGPUBeginRenderPassInfo begin =
			{
				.render_pass = runtime.renderPass->renderPass,
				.framebuffer = runtime.framebuffer->framebuffer,
				.clear_value_count = clear_value_count,
				.clear_values = clear_values,
			};
			auto encoder = cgpu_cmd_begin_render_pass(cmd, &begin);
			auto state_buffer = cgpu_create_state_buffer(cmd, nullptr);
			cgpu_render_encoder_bind_state_buffer(encoder, state_buffer);
			auto raster_state_encoder = cgpu_open_raster_state_encoder(state_buffer, encoder);

			cgpu_render_encoder_set_viewport(encoder,
				0.0f, 0.0f,
				(float)fbDesc.width, (float)fbDesc.height,
				0.f, 1.f);
			cgpu_render_encoder_set_scissor(encoder, 0, 0, fbDesc.width, fbDesc.height);
			if (context.support_shading_rate)
				cgpu_render_encoder_set_shading_rate(encoder, CGPU_SHADING_RATE_FULL, CGPU_SHADING_RATE_COMBINER_PASSTHROUGH, CGPU_SHADING_RATE_COMBINER_PASSTHROUGH);

			if (pass.executable)
			{
				RenderPassEncoder rg_encoder = {
					.encoder = encoder,
					.state_buffer = state_buffer,
					.raster_state_encoder = raster_state_encoder,
					.render_pass = runtime.renderPass->renderPass,
					.subpass = 0,
					.render_target_count = (uint32_t)pass.colorAttachmentCount,
					.context = &context,
					.compiled_graph = &compiledRenderGraph,
					.last_render_pipeline = 0,
					.last_bind_resources = {0},
				};
				pass.executable(&rg_encoder, pass.passdata);
			}

			cgpu_close_raster_state_encoder(raster_state_encoder);
			cgpu_free_state_buffer(state_buffer);
			cgpu_cmd_end_render_pass(cmd, encoder);
		}

	}

	void execute_compute_pass(ExecutorContext& context, CompiledRenderGraph& compiledRenderGraph, const CompiledRenderPassNode& pass, RuntimePass& runtime, CGPUCommandBufferId cmd)
	{
		CGPUComputePassDescriptor pass_desc =
		{
			.name = pass.name
		};
		auto encoder = cgpu_cmd_begin_compute_pass(cmd, &pass_desc);

		if (pass.executable)
		{
			RenderPassEncoder rg_encoder = {
				.compute_encoder = encoder,
				.context = &context,
				.compiled_graph = &compiledRenderGraph,
				.last_render_pipeline = 0,
				.last_bind_resources = {0},
			};
			pass.executable(&rg_encoder, pass.passdata);
		}

		cgpu_cmd_end_compute_pass(cmd, encoder);
	}

	void execute_upload_texture_pass(ExecutorContext& context, CompiledRenderGraph& compiledRenderGraph, const CompiledRenderPassNode& pass, RuntimePass& runtime, CGPUCommandBufferId cmd)
	{
		auto& src_resource_node = compiledRenderGraph.resources[pass.staging_buffer];
		CGPUBufferId src_buffer = src_resource_node.manageType == ManageType::Managed ? src_resource_node.managed_buffer->handle : src_resource_node.imported_buffer->handle;

		if (pass.size > 0 && pass.data)
		{
			auto address = (char*)src_buffer->info->cpu_mapped_address + pass.offset;
			memcpy(address, pass.data, pass.size);
		}

		if (pass.uploadTextureExecutable)
		{
			UploadEncoder up_encoder = {
				.size = src_buffer->info->size,
				.address = src_buffer->info->cpu_mapped_address,
			};

			pass.uploadTextureExecutable(&up_encoder, pass.passdata);
		}

		auto& dest_resource_node = compiledRenderGraph.resources[pass.dest_texture];
		auto dest_texture = getTexture(compiledRenderGraph.resources, dest_resource_node);

		CGPUBufferToTextureTransfer b2t = {};
		b2t.src = src_buffer;
		b2t.src_offset = 0;
		b2t.dst = dest_texture->handle;
		b2t.dst_subresource.mip_level = pass.mipmap;
		b2t.dst_subresource.base_array_layer = pass.slice;
		b2t.dst_subresource.layer_count = 1;
		cgpu_cmd_transfer_buffer_to_texture(cmd, &b2t);
	}

	void execute_upload_buffer_pass(ExecutorContext& context, CompiledRenderGraph& compiledRenderGraph, const CompiledRenderPassNode& pass, RuntimePass& runtime, CGPUCommandBufferId cmd)
	{
		auto& src_resource_node = compiledRenderGraph.resources[pass.staging_buffer];
		CGPUBufferId src_buffer = src_resource_node.manageType == ManageType::Managed ? src_resource_node.managed_buffer->handle : src_resource_node.imported_buffer->handle;

		if (pass.size > 0 && pass.data)
		{
			auto address = (char*)src_buffer->info->cpu_mapped_address + pass.offset;
			memcpy(address, pass.data, pass.size);
		}

		if (pass.uploadTextureExecutable)
		{
			UploadEncoder up_encoder = {
				.size = src_buffer->info->size,
				.address = src_buffer->info->cpu_mapped_address,
			};

			pass.uploadTextureExecutable(&up_encoder, pass.passdata);
		}

		auto& dest_resource_node = compiledRenderGraph.resources[pass.dest_buffer];
		auto dest_buffer = dest_resource_node.manageType == ManageType::Managed ? dest_resource_node.managed_buffer->handle : dest_resource_node.imported_buffer->handle;

		CGPUBufferToBufferTransfer b2b = {};
		b2b.src = src_buffer;
		b2b.src_offset = 0;
		b2b.dst = dest_buffer;
		b2b.dst_offset = 0;
		b2b.size = dest_buffer->info->size;
		cgpu_cmd_transfer_buffer_to_buffer(cmd, &b2b);
	}

	void Executor::Execute(CompiledRenderGraph& compiledRenderGraph, ExecutorContext& context)
	{
		auto cmd = context.requestCmd();

		cgpu_cmd_begin(cmd);

		if (context.profiler)
		{
			context.profiler->CollectTimings();
			context.profiler->OnBeginFrame(cmd);
		}

		std::pmr::vector<RuntimePass> runtimePasses(context.memory_resource);

		for (auto i = 0; i < compiledRenderGraph.passes.size(); ++i)
		{
			auto& pass = compiledRenderGraph.passes[i];

			RuntimePass runtime = {};
			runtime.passNode = &pass;

			for (auto resourceIndex : pass.devirtualize)
			{
				auto& resource = compiledRenderGraph.resources[resourceIndex];
				if (resource.resourceType == ResourceType::Texture)
				{
					if (resource.manageType == ManageType::Managed)
					{
						resource.managered_texture = context.texturePool.getTexture(resource.width, resource.height, resource.depth, resource.format);
					}
				}
				else if (resource.resourceType == ResourceType::Buffer)
				{
					if (resource.manageType == ManageType::Managed)
					{
						CGPUBufferDescriptor desc = {};
						desc.name = resource.name;
						desc.flags = resource.memoryUsage != CGPU_MEM_USAGE_GPU_ONLY ? CGPU_BCF_PERSISTENT_MAP_BIT : CGPU_BCF_NONE;
						desc.descriptors = resource.bufferType;
						desc.memory_usage = resource.memoryUsage;
						desc.size = resource.size;

						resource.managed_buffer = context.bufferPool.getResource(desc);
					}
				}
			}

			place_barriers(context, compiledRenderGraph, pass, runtime, cmd);
			if (pass.type == PASS_TYPE_RENDER)
			{
				execute_render_pass(context, compiledRenderGraph, pass, runtime, cmd);
			}
			else if (pass.type == PASS_TYPE_COMPUTE)
			{
				execute_compute_pass(context, compiledRenderGraph, pass, runtime, cmd);
			}
			else if (pass.type == PASS_TYPE_UPLOAD_TEXTURE)
			{
				execute_upload_texture_pass(context, compiledRenderGraph, pass, runtime, cmd);
			}
			else if (pass.type == PASS_TYPE_UPLOAD_BUFFER)
			{
				execute_upload_buffer_pass(context, compiledRenderGraph, pass, runtime, cmd);
			}

			for (auto resourceIndex : pass.destroy)
			{
				auto& resource = compiledRenderGraph.resources[resourceIndex];
				if (resource.resourceType == ResourceType::Texture)
				{
					if (resource.manageType == ManageType::Managed)
						context.texturePool.releaseResource(resource.managered_texture);
				}
				else if (resource.resourceType == ResourceType::Buffer)
				{
					if (resource.manageType == ManageType::Managed)
						context.bufferPool.releaseResource(resource.managed_buffer);
				}
			}

			if (context.profiler)context.profiler->GetTimeStamp(cmd, pass.name);
			runtimePasses.push_back(runtime);
		}

		runtimePasses.clear();

		if (context.profiler)context.profiler->OnEndFrame(cmd);
		cgpu_cmd_end(cmd);
	}
}