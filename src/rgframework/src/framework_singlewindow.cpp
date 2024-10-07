#include "framework.h"

#include <SDL_syswm.h>
#include "cgpu/api.h"
#include "rendergraph.h"
#include "rendergraph_compiler.h"
#include "rendergraph_executor.h"
#include "imgui_impl_sdl2.h"
#include <time.h>
#include "tiny_obj_loader.h"
#include <string.h>
#include "cgpu_device.h"
#ifdef __linux__
#include <unistd.h>
#endif

void oval_log(void* user_data, ECGPULogSeverity severity, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

#ifdef _WIN32
static size_t malloced = 0;
void* oval_malloc(void* user_data, size_t size, const void* pool)
{
	if (size == 0)
		return nullptr;
	malloced += size;
	return malloc(size);
}

void* oval_realloc(void* user_data, void* ptr, size_t size, const void* pool)
{
	malloced -= ptr ? _msize(ptr) : 0;
	malloced += size;
	return realloc(ptr, size);
}

void* oval_calloc(void* user_data, size_t count, size_t size, const void* pool)
{
	if (count * size == 0)
		return nullptr;
	malloced += count * size;
	return calloc(count, size);
}

void oval_free(void* user_data, void* ptr, const void* pool)
{
	malloced -= ptr ? _msize(ptr) : 0;
	free(ptr);
}

static size_t aligned_malloced = 0;
void* oval_malloc_aligned(void* user_data, size_t size, size_t alignment, const void* pool)
{
	aligned_malloced += size;
	return _aligned_malloc(size, alignment);
}

void* oval_realloc_aligned(void* user_data, void* ptr, size_t size, size_t alignment, const void* pool)
{
	aligned_malloced -= ptr ? _aligned_msize(ptr, alignment, 0) : 0;
	aligned_malloced += size;
	return _aligned_realloc(ptr, size, alignment);
}

void* oval_calloc_aligned(void* user_data, size_t count, size_t size, size_t alignment, const void* pool)
{
	aligned_malloced += count * size;
	void* memory = _aligned_malloc(count * size, alignment);
	if (memory != NULL) memset(memory, 0, count * size);
	return memory;
}

void oval_free_aligned(void* user_data, void* ptr, const void* pool)
{
	aligned_malloced -= ptr ? _aligned_msize(ptr, 1, 0) : 0;
	_aligned_free(ptr);
}
#endif

oval_device_t* oval_create_device(const oval_device_descriptor* device_descriptor)
{
	if (SDL_Init(0) < 0)
		return nullptr;

	SDL_Window* window = SDL_CreateWindow("oval", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, device_descriptor->width, device_descriptor->height, SDL_WINDOW_SHOWN);
	if (window == nullptr)
	{
		SDL_Quit();
		return nullptr;
	}

	auto memory_resource = new std::pmr::unsynchronized_pool_resource();
	oval_device_t super = { .descriptor = *device_descriptor, .deltaTime = 0 };

	auto device_cgpu = new oval_cgpu_device_t(super, memory_resource);
	device_cgpu->window = window;

	if (device_descriptor->enable_capture)
	{
		auto renderdoc_path = locate_renderdoc();
		if (load_renderdoc(renderdoc_path))
			device_cgpu->rdc = GetRenderDocApi();
	}

	CGPUInstanceDescriptor instance_desc = {
		.backend = CGPU_BACKEND_VULKAN,
		.enable_debug_layer = true,
		.enable_gpu_based_validation = true,
		.enable_set_name = true,
		.logger = {
			.log_callback = oval_log
		},
#ifdef _WIN32
		.allocator = {
			.malloc_fn = oval_malloc,
			.realloc_fn = oval_realloc,
			.calloc_fn = oval_calloc,
			.free_fn = oval_free,
			.malloc_aligned_fn = oval_malloc_aligned,
			.realloc_aligned_fn = oval_realloc_aligned,
			.calloc_aligned_fn = oval_calloc_aligned,
			.free_aligned_fn = oval_free_aligned,
		},
#endif
	};

	device_cgpu->instance = cgpu_create_instance(&instance_desc);

	uint32_t adapters_count = 0;
	cgpu_enum_adapters(device_cgpu->instance, CGPU_NULLPTR, &adapters_count);
	CGPUAdapterId* adapters = (CGPUAdapterId*)malloc(sizeof(CGPUAdapterId) * (adapters_count));
	cgpu_enum_adapters(device_cgpu->instance, adapters, &adapters_count);
	auto adapter = adapters[0];

	// Create device
	CGPUQueueGroupDescriptor G = {
		.queue_type = CGPU_QUEUE_TYPE_GRAPHICS,
		.queue_count = 1
	};
	CGPUDeviceDescriptor device_desc = {
		.queue_groups = &G,
		.queue_group_count = 1
	};
	device_cgpu->device = cgpu_create_device(adapter, &device_desc);
	device_cgpu->gfx_queue = cgpu_get_queue(device_cgpu->device, CGPU_QUEUE_TYPE_GRAPHICS, 0);
	device_cgpu->present_queue = device_cgpu->gfx_queue;
	free(adapters);
	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(window, &wmInfo);
#ifdef _WIN32
	device_cgpu->surface = cgpu_surface_from_native_view(device_cgpu->device, wmInfo.info.win.window);
#endif

	int w, h;
	SDL_GetWindowSize(device_cgpu->window, &w, &h);

	ECGPUFormat swapchainFormat = CGPU_FORMAT_R8G8B8A8_SRGB;
	CGPUSwapChainDescriptor descriptor = {
		.present_queues = &device_cgpu->present_queue,
		.present_queues_count = 1,
		.surface = device_cgpu->surface,
		.image_count = 3,
		.width = (uint32_t)w,
		.height = (uint32_t)h,
		.enable_vsync = true,
		.format = swapchainFormat,
	};
	device_cgpu->swapchain = cgpu_create_swapchain(device_cgpu->device, &descriptor);

	for (uint32_t i = 0; i < device_cgpu->swapchain->buffer_count; i++)
	{
		HGEGraphics::init_backbuffer(device_cgpu->backbuffer + i, device_cgpu->swapchain, i);
		device_cgpu->swapchain_prepared_semaphores[i] = cgpu_create_semaphore(device_cgpu->device);
	}

	for (size_t i = 0; i < device_cgpu->swapchain->buffer_count; ++i)
		device_cgpu->frameDatas.emplace_back(device_cgpu->device, device_cgpu->gfx_queue, device_descriptor->enable_profile, device_cgpu->memory_resource);

	device_cgpu->render_finished_semaphore = cgpu_create_semaphore(device_cgpu->device);

	{
		const uint64_t width = 4;
		const uint64_t height = 4;
		const uint64_t count = width * height;

		CGPUTextureDescriptor default_texture_desc =
		{
			.name = u8"default_texture",
			.width = width,
			.height = height,
			.depth = 1,
			.array_size = 1,
			.format = CGPU_FORMAT_R8G8B8A8_UNORM,
			.mip_levels = 1,
			.descriptors = CGPU_RESOURCE_TYPE_TEXTURE,
		};
		uint32_t colors[count];
		std::fill(colors, colors + count, 0xffff00ff);
		device_cgpu->default_texture = oval_create_texture_from_buffer(&device_cgpu->super, default_texture_desc, colors, sizeof(colors));
		for (int i = 0; i < 3; ++i)
		{
			device_cgpu->frameDatas[i].execContext.default_texture = device_cgpu->default_texture->view;
		}
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	ImGui::StyleColorsLight();

	ImGui_ImplSDL2_InitForOther(window);
	{
		ImGuiIO& io = ImGui::GetIO();
		unsigned char* fontPixels;
		int fontTexWidth, fontTexHeight;
		io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontTexWidth, &fontTexHeight);
	}

	CGPUBlendStateDescriptor blit_blend_desc = {
		.src_factors = { CGPU_BLEND_CONST_ONE },
		.dst_factors = { CGPU_BLEND_CONST_ZERO },
		.src_alpha_factors = { CGPU_BLEND_CONST_ONE },
		.dst_alpha_factors = { CGPU_BLEND_CONST_ZERO },
		.blend_modes = { CGPU_BLEND_MODE_ADD },
		.blend_alpha_modes = { CGPU_BLEND_MODE_ADD },
		.masks = { CGPU_COLOR_MASK_ALL },
		.alpha_to_coverage = false,
		.independent_blend = false,
	};
	CGPUDepthStateDesc depth_desc = {
		.depth_test = false,
		.depth_write = false,
		.stencil_test = false,
	};
	CGPURasterizerStateDescriptor rasterizer_state = {
		.cull_mode = CGPU_CULL_MODE_NONE,
	};
	uint8_t blit_vert_spv[] = {
		#include "blit.vs.spv.h"
	};
	uint8_t blit_frag_spv[] = {
		#include "blit.ps.spv.h"
	};
	device_cgpu->blit_shader = HGEGraphics::create_shader(device_cgpu->device, blit_vert_spv, sizeof(blit_vert_spv), blit_frag_spv, sizeof(blit_frag_spv), blit_blend_desc, depth_desc, rasterizer_state);

	CGPUSamplerDescriptor blit_linear_sampler_desc = {
		.min_filter = CGPU_FILTER_TYPE_LINEAR,
		.mag_filter = CGPU_FILTER_TYPE_LINEAR,
		.mipmap_mode = CGPU_MIPMAP_MODE_LINEAR,
		.address_u = CGPU_ADDRESS_MODE_CLAMP_TO_EDGE,
		.address_v = CGPU_ADDRESS_MODE_CLAMP_TO_EDGE,
		.address_w = CGPU_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mip_lod_bias = 0,
		.max_anisotropy = 1,
	};
	device_cgpu->blit_linear_sampler = cgpu_create_sampler(device_cgpu->device, &blit_linear_sampler_desc);

	CGPUBlendStateDescriptor imgui_blend_desc = {
		.src_factors = { CGPU_BLEND_CONST_SRC_ALPHA },
		.dst_factors = { CGPU_BLEND_CONST_ONE_MINUS_SRC_ALPHA },
		.src_alpha_factors = { CGPU_BLEND_CONST_SRC_ALPHA },
		.dst_alpha_factors = { CGPU_BLEND_CONST_ONE_MINUS_SRC_ALPHA },
		.blend_modes = { CGPU_BLEND_MODE_ADD },
		.blend_alpha_modes = { CGPU_BLEND_MODE_ADD },
		.masks = { CGPU_COLOR_MASK_ALL },
		.alpha_to_coverage = false,
		.independent_blend = false,
	};
	uint8_t imgui_vert_spv[] = {
		#include "imgui.vs.spv.h"
	};
	uint8_t imgui_frag_spv[] = {
		#include "imgui.ps.spv.h"
	};
	device_cgpu->imgui_shader = HGEGraphics::create_shader(device_cgpu->device, imgui_vert_spv, sizeof(imgui_vert_spv), imgui_frag_spv, sizeof(imgui_frag_spv), imgui_blend_desc, depth_desc, rasterizer_state);

	CGPUVertexLayout imgui_vertex_layout =
	{
		.attribute_count = 3,
		.attributes =
		{
			{ u8"POSITION", 1, CGPU_FORMAT_R32G32_SFLOAT, 0, 0, sizeof(float) * 2, CGPU_INPUT_RATE_VERTEX },
			{ u8"TEXCOORD", 1, CGPU_FORMAT_R32G32_SFLOAT, 0, sizeof(float) * 2, sizeof(float) * 2, CGPU_INPUT_RATE_VERTEX },
			{ u8"COLOR", 1, CGPU_FORMAT_R8G8B8A8_UNORM, 0, sizeof(float) * 4, sizeof(uint32_t), CGPU_INPUT_RATE_VERTEX },
		}
	};
	device_cgpu->imgui_mesh = HGEGraphics::create_dynamic_mesh(CGPU_PRIM_TOPO_TRI_LIST, imgui_vertex_layout, sizeof(ImDrawIdx));

	{
		unsigned char* fontPixels;
		int fontTexWidth, fontTexHeight;
		io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontTexWidth, &fontTexHeight);

		uint64_t width = fontTexWidth;
		uint64_t height = fontTexHeight;
		uint64_t count = width * height;

		CGPUTextureDescriptor imgui_font_texture_desc =
		{
			.name = u8"ImGui Default Font Texture",
			.width = width,
			.height = height,
			.depth = 1,
			.array_size = 1,
			.format = CGPU_FORMAT_R8G8B8A8_UNORM,
			.mip_levels = 1,
			.descriptors = CGPU_RESOURCE_TYPE_TEXTURE,
		};
		device_cgpu->imgui_font_texture = oval_create_texture_from_buffer(&device_cgpu->super, imgui_font_texture_desc, fontPixels, count * 4);
	}

	CGPUSamplerDescriptor imgui_font_sampler_desc = {
		.min_filter = CGPU_FILTER_TYPE_LINEAR,
		.mag_filter = CGPU_FILTER_TYPE_LINEAR,
		.mipmap_mode = CGPU_MIPMAP_MODE_LINEAR,
		.address_u = CGPU_ADDRESS_MODE_REPEAT,
		.address_v = CGPU_ADDRESS_MODE_REPEAT,
		.address_w = CGPU_ADDRESS_MODE_REPEAT,
		.mip_lod_bias = 0,
		.max_anisotropy = 1,
	};
	device_cgpu->imgui_font_sampler = cgpu_create_sampler(device_cgpu->device, &imgui_font_sampler_desc);

	return (oval_device_t*)device_cgpu;
}

HGEGraphics::Mesh* setupImGuiResourcesMesh(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg)
{
	using namespace HGEGraphics;

	ImDrawData* drawData = ImGui::GetDrawData();
	if (drawData && drawData->TotalVtxCount > 0)
	{
		size_t vertex_size = drawData->TotalVtxCount * sizeof(ImDrawVert);
		size_t index_size = drawData->TotalIdxCount * sizeof(ImDrawIdx);

		auto imgui_vertex_buffer = declare_dynamic_vertex_buffer(device->imgui_mesh, &rg, drawData->TotalVtxCount);
		rendergraph_add_uploadbufferpass(&rg, u8"upload imgui vertex data", imgui_vertex_buffer, [](UploadEncoder* encoder, void* passdata)
			{
				ImDrawData* drawData = ImGui::GetDrawData();

				uint32_t offset = 0;
				for (int n = 0; n < drawData->CmdListsCount; n++)
				{
					const ImDrawList* cmd_list = drawData->CmdLists[n];
					upload(encoder, offset, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert), cmd_list->VtxBuffer.Data);

					offset += cmd_list->VtxBuffer.Size;
				}
			}, 0, nullptr);

		auto imgui_index_buffer = declare_dynamic_index_buffer(device->imgui_mesh, &rg, drawData->TotalIdxCount);
		rendergraph_add_uploadbufferpass(&rg, u8"upload imgui index data", imgui_index_buffer, [](UploadEncoder* encoder, void* passdata)
			{
				ImDrawData* drawData = ImGui::GetDrawData();

				uint32_t offset = 0;
				for (int n = 0; n < drawData->CmdListsCount; n++)
				{
					const ImDrawList* cmd_list = drawData->CmdLists[n];
					upload(encoder, offset, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx), cmd_list->IdxBuffer.Data);

					offset += cmd_list->IdxBuffer.Size;
				}
			}, 0, nullptr);
	}
	return device->imgui_mesh;
}

HGEGraphics::Mesh* setupImGuiResources(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg)
{
	auto imgui_mesh = setupImGuiResourcesMesh(device, rg);
	ImDrawData* drawData = ImGui::GetDrawData();
	device->imgui_draw_data = (drawData && drawData->TotalVtxCount > 0) ? drawData : nullptr;
	return imgui_mesh;
}

void renderImgui(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::texture_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	if (device->imgui_draw_data)
	{
		auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
		uint32_t color = 0xffffffff;
		renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_LOAD, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
		renderpass_use_buffer(&passBuilder, device->imgui_mesh->vertex_buffer->dynamic_handle);
		renderpass_use_buffer(&passBuilder, device->imgui_mesh->index_buffer->dynamic_handle);

		void* passdata = nullptr;
		renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* passdata)
			{
				oval_cgpu_device_t* device = *(oval_cgpu_device_t**)passdata;

				float scale[2];
				scale[0] = 2.0f / device->imgui_draw_data->DisplaySize.x;
				scale[1] = -2.0f / device->imgui_draw_data->DisplaySize.y;
				float translate[2];
				translate[0] = -1.0f - device->imgui_draw_data->DisplayPos.x * scale[0];
				translate[1] = +1.0f - device->imgui_draw_data->DisplayPos.y * scale[1];
				struct ConstantData
				{
					float scale[2];
					float translate[2];
				} data;
				data = {
					.scale = { scale[0], scale[1] },
					.translate = { translate[0], translate[1] },
				};
				push_constants(encoder, device->imgui_shader, u8"pc", &data);

				set_global_texture(encoder, device->imgui_font_texture, 0, 0);
				set_global_sampler(encoder, device->imgui_font_sampler, 0, 1);

				auto drawData = device->imgui_draw_data;
				int global_vtx_offset = 0;
				int global_idx_offset = 0;
				for (size_t i = 0; i < drawData->CmdListsCount; ++i)
				{
					const auto cmdList = drawData->CmdLists[i];
					for (size_t j = 0; j < cmdList->CmdBuffer.size(); ++j)
					{
						const auto cmdBuffer = &cmdList->CmdBuffer[j];
						draw_submesh(encoder, device->imgui_shader, device->imgui_mesh, cmdBuffer->ElemCount, cmdBuffer->IdxOffset + global_idx_offset, 0, cmdBuffer->VtxOffset + global_vtx_offset);
					}
					global_idx_offset += cmdList->IdxBuffer.Size;
					global_vtx_offset += cmdList->VtxBuffer.Size;
				}
			}, sizeof(int*), &passdata);
		*(oval_cgpu_device_t**)passdata = device;
	}
}

void render(oval_cgpu_device_t* device, HGEGraphics::Backbuffer* backbuffer)
{
	using namespace HGEGraphics;

	std::pmr::unsynchronized_pool_resource rg_pool(device->memory_resource);
	rendergraph_t rg{ 1, 1, 1, device->blit_shader, device->blit_linear_sampler, &rg_pool };

	oval_graphics_transfer_queue_execute_all(device, rg);

	auto rg_back_buffer = rendergraph_import_backbuffer(&rg, backbuffer);

	auto imgui_mesh = setupImGuiResources(device, rg);

	if (device->super.descriptor.on_draw)
		device->super.descriptor.on_draw(&device->super, rg, rg_back_buffer);
	renderImgui(device, rg, rg_back_buffer);

	rendergraph_present(&rg, rg_back_buffer);

	auto compiled = Compiler::Compile(rg, &rg_pool);
	Executor::Execute(compiled, device->frameDatas[device->current_frame_index].execContext);

	for (auto imported : rg.imported_textures)
	{
		imported->dynamic_handle = {};
	}
	for (auto imported : rg.imported_buffers)
	{
		imported->dynamic_handle = {};
	}

	oval_graphics_transfer_queue_release_all(device);
}

bool on_resize(oval_cgpu_device_t* D)
{
	for (uint32_t i = 0; i < 3; i++)
	{
		HGEGraphics::free_backbuffer(D->backbuffer + i);
	}

	if (D->swapchain)
		cgpu_free_swapchain(D->swapchain);
	D->swapchain = CGPU_NULLPTR;

	if (SDL_GetWindowFlags(D->window) & SDL_WINDOW_MINIMIZED)
		return false;;

	int w, h;
	SDL_GetWindowSize(D->window, &w, &h);

	if (w == 0 || h == 0)
		return false;

	ECGPUFormat swapchainFormat = CGPU_FORMAT_R8G8B8A8_SRGB;
	CGPUSwapChainDescriptor descriptor = {
		.present_queues = &D->present_queue,
		.present_queues_count = 1,
		.surface = D->surface,
		.image_count = 3,
		.width = (uint32_t)w,
		.height = (uint32_t)h,
		.enable_vsync = true,
		.format = swapchainFormat,
	};
	D->swapchain = cgpu_create_swapchain(D->device, &descriptor);
	for (uint32_t i = 0; i < D->swapchain->buffer_count; i++)
	{
		HGEGraphics::init_backbuffer(D->backbuffer + i, D->swapchain, i);
	}

	return true;
}

void oval_runloop(oval_device_t* device)
{
	auto D = (oval_cgpu_device_t*)device;

	SDL_Event e;
	bool quit = false;
	bool requestResize = false;

	D->current_frame_index = 0;
	clock_t lastTime = clock();

	while (quit == false)
	{
#ifdef _WIN32
		_sleep(0);
#else
		sleep(0);
#endif

		while (SDL_PollEvent(&e))
		{
			ImGui_ImplSDL2_ProcessEvent(&e);
			if (e.type == SDL_QUIT)
				quit = true;
			else if (e.type == SDL_WINDOWEVENT)
			{
				if (e.window.windowID == SDL_GetWindowID(D->window))
				{
					if (e.window.event == SDL_WINDOWEVENT_CLOSE)
						quit = true;
					else if (e.window.event == SDL_WINDOWEVENT_RESIZED || e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
						requestResize = true;
				}
			}
		}

		if (requestResize)
		{
			cgpu_wait_queue_idle(D->gfx_queue);
			requestResize = !on_resize(D);
		}

		if (requestResize)
			continue;

		bool rdc_capturing = false;
		if (D->rdc && D->rdc_capture)
		{
			D->rdc->StartFrameCapture(nullptr, nullptr);
			rdc_capturing = true;
		}

		auto& cur_frame_data = D->frameDatas[D->current_frame_index];
		cgpu_wait_fences(&cur_frame_data.inflightFence, 1);
		cur_frame_data.newFrame();
		D->info.reset();

		CGPUAcquireNextDescriptor acquire_desc = {
			.signal_semaphore = D->swapchain_prepared_semaphores[D->current_frame_index],
		};

		auto acquired_swamchin_index = cgpu_acquire_next_image(D->swapchain, &acquire_desc);

		if (acquired_swamchin_index < D->swapchain->buffer_count)
			D->info.current_swapchain_index = acquired_swamchin_index;
		else
			requestResize = true;

		if (requestResize)
		{
			continue;
		}

		clock_t currentTime = clock();
		double elapsedTime = (double)(currentTime - lastTime) / CLOCKS_PER_SEC;
		lastTime = currentTime;
		D->super.deltaTime = elapsedTime;

		if (D->super.descriptor.on_update)
			D->super.descriptor.on_update(&D->super);

		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		if (D->super.descriptor.on_imgui)
			D->super.descriptor.on_imgui(&D->super);

		ImGui::EndFrame();
		ImGui::Render();

		auto back_buffer = &D->backbuffer[D->info.current_swapchain_index];
		auto prepared_semaphore = D->swapchain_prepared_semaphores[D->current_frame_index];

		if (D->cur_transfer_queue)
			oval_graphics_transfer_queue_submit(device, D->cur_transfer_queue);
		D->cur_transfer_queue = nullptr;
		oval_process_load_queue(D);

		render(D, back_buffer);

		CGPUQueueSubmitDescriptor submit_desc = {
			.cmds = cur_frame_data.execContext.allocated_cmds.data(),
			.signal_fence = cur_frame_data.inflightFence,
			.wait_semaphores = &prepared_semaphore,
			.signal_semaphores = &D->render_finished_semaphore,
			.cmds_count = (uint32_t)cur_frame_data.execContext.allocated_cmds.size(),
			.wait_semaphore_count = 1,
			.signal_semaphore_count = 1,
		};
		cgpu_submit_queue(D->gfx_queue, &submit_desc);

		CGPUQueuePresentDescriptor present_desc = {
			.swapchain = D->swapchain,
			.wait_semaphores = &D->render_finished_semaphore,
			.wait_semaphore_count = 1,
			.index = (uint8_t)D->info.current_swapchain_index,
		};
		cgpu_queue_present(D->present_queue, &present_desc);

		D->current_frame_index = (D->current_frame_index + 1) % D->swapchain->buffer_count;

		if (rdc_capturing)
		{
			D->rdc->EndFrameCapture(nullptr, nullptr);
			D->rdc_capture = false;
		}
		if (D->rdc && D->rdc_capture)
		{
			if (!D->rdc->IsRemoteAccessConnected())
			{
				D->rdc->LaunchReplayUI(1, "");
			}
		}
	}

	cgpu_wait_queue_idle(D->gfx_queue);

	for (int i = 0; i < 3; ++i)
	{
		D->frameDatas[i].execContext.pre_destroy();
	}
}

void oval_free_device(oval_device_t* device)
{
	auto D = (oval_cgpu_device_t*)device;

	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();

	if (D->default_texture)
		free_texture(D->default_texture);

	if (D->imgui_shader)
		free_shader(D->imgui_shader);
	D->imgui_shader = nullptr;

	if (D->imgui_mesh)
		free_mesh(D->imgui_mesh);
	D->imgui_mesh = nullptr;

	if (D->imgui_font_texture)
		free_texture(D->imgui_font_texture);
	D->imgui_font_texture = nullptr;

	if (D->imgui_font_sampler)
		cgpu_free_sampler(D->imgui_font_sampler);
	D->imgui_font_sampler = CGPU_NULLPTR;

	if (D->blit_shader)
		free_shader(D->blit_shader);
	D->blit_shader = nullptr;

	if (D->blit_linear_sampler)
		cgpu_free_sampler(D->blit_linear_sampler);
	D->blit_linear_sampler = nullptr;

	for (uint32_t i = 0; i < D->swapchain->buffer_count; i++)
	{
		cgpu_free_semaphore(D->swapchain_prepared_semaphores[i]);
		D->swapchain_prepared_semaphores[i] = CGPU_NULLPTR;
		HGEGraphics::free_backbuffer(D->backbuffer + i);
	}

	cgpu_free_swapchain(D->swapchain);
	D->swapchain = CGPU_NULLPTR;
	cgpu_free_surface(D->device, D->surface);
	D->surface = CGPU_NULLPTR;

	cgpu_free_semaphore(D->render_finished_semaphore);
	D->render_finished_semaphore = CGPU_NULLPTR;

	D->info.reset();
	D->current_frame_index = -1;

	for (int i = 0; i < 3; ++i)
	{
		D->frameDatas[i].free();
	}

	cgpu_free_queue(D->gfx_queue);
	D->gfx_queue = CGPU_NULLPTR;
	D->present_queue = CGPU_NULLPTR;
	cgpu_free_device(D->device);
	D->device = CGPU_NULLPTR;
	cgpu_free_instance(D->instance);
	D->instance = CGPU_NULLPTR;

	auto memory = D->memory_resource;

	SDL_DestroyWindow(D->window);
	D->window = CGPU_NULLPTR;

	SDL_Quit();

	D->super.deltaTime = 0;

	delete D;

	delete memory;
}

void oval_render_debug_capture(oval_device_t* device)
{
	auto D = (oval_cgpu_device_t*)device;
	D->rdc_capture = true;
}

void oval_query_render_profile(oval_device_t* device, uint32_t* length, const char8_t*** names, const float** durations)
{
	auto D = (oval_cgpu_device_t*)device;
	auto& cur_frame_data = D->frameDatas[D->current_frame_index];
	if (cur_frame_data.execContext.profiler)
	{
		cur_frame_data.execContext.profiler->Query(*length, *names, *durations);
	}
	else
	{
		*length = 0;
		*names = nullptr;
		*durations = nullptr;
	}
}
