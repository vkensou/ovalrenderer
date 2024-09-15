#include "framework.h"

#include <SDL.h>
#include <SDL_syswm.h>
#include "cgpu/api.h"
#include "rendergraph.h"
#include "rendergraph_compiler.h"
#include "rendergraph_executor.h"
#include "renderer.h"
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "renderdoc_helper.h"
#include <time.h>
#include "stb_image.h"
#include "tiny_obj_loader.h"
#include <string.h>
#include <queue>
#include "ktx.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

struct FrameData
{
	CGPUFenceId inflightFence;
	HGEGraphics::ExecutorContext execContext;

	FrameData(CGPUDeviceId device, CGPUQueueId gfx_queue, bool profile, std::pmr::memory_resource* memory_resource)
		: execContext(device, gfx_queue, profile, memory_resource)
	{
		inflightFence = cgpu_create_fence(device);
	}

	void newFrame()
	{
		execContext.newFrame();
	}

	void free()
	{
		execContext.destroy();

		cgpu_free_fence(inflightFence);
		inflightFence = CGPU_NULLPTR;
	}
};

struct FrameInfo
{
	uint16_t current_swapchain_index;

	void reset()
	{
		current_swapchain_index = -1;
	}
};

struct WaitUploadTexture
{
	HGEGraphics::Texture* texture;
	int loader_type;
	stbi_uc* loader;
	ktxTexture* ktxTexture;
	bool generate_mipmap;
	int component;
};

struct WaitUploadMesh
{
	HGEGraphics::Mesh* mesh;
	std::pmr::vector<TexturedVertex>* vertex_data;
	std::pmr::vector<uint32_t>* index_data;
};

typedef struct oval_cgpu_device_t {
	oval_cgpu_device_t(const oval_device_t& super, std::pmr::memory_resource* memory_resource)
		: super(super), memory_resource(memory_resource), wait_upload_texture(memory_resource), wait_upload_mesh(memory_resource), delay_released_stbi_loader(memory_resource), delay_released_ktxTexture(memory_resource), delay_released_vertex_buffer(memory_resource), delay_released_index_buffer(memory_resource)
	{
	}

	oval_device_t super;
	SDL_Window* window;
	std::pmr::memory_resource* memory_resource;
	CGPUInstanceId instance;
	CGPUDeviceId device;
	CGPUQueueId gfx_queue;
	CGPUQueueId present_queue;

	CGPUSurfaceId surface;
	CGPUSwapChainId swapchain;
	HGEGraphics::Backbuffer backbuffer[3];
	CGPUSemaphoreId swapchain_prepared_semaphores[3];

	std::vector<FrameData> frameDatas;
	CGPUSemaphoreId render_finished_semaphore;
	uint32_t current_frame_index;
	FrameInfo info;

	HGEGraphics::Shader* blit_shader = nullptr;
	CGPUSamplerId blit_linear_sampler = CGPU_NULLPTR;

	HGEGraphics::Texture* imgui_font_texture = nullptr;
	HGEGraphics::Shader* imgui_shader = nullptr;
	CGPUSamplerId imgui_font_sampler = CGPU_NULLPTR;
	HGEGraphics::Mesh* imgui_mesh = nullptr;

	ImDrawData* imgui_draw_data = nullptr;

	bool rdc_capture = false;
	RENDERDOC_API_1_0_0* rdc = nullptr;

	std::queue<WaitUploadTexture, std::pmr::deque<WaitUploadTexture>> wait_upload_texture;
	std::queue<WaitUploadMesh, std::pmr::deque<WaitUploadMesh>> wait_upload_mesh;
	std::pmr::vector<stbi_uc*> delay_released_stbi_loader;
	std::pmr::vector<ktxTexture*> delay_released_ktxTexture;
	std::pmr::vector<std::pmr::vector<TexturedVertex>*> delay_released_vertex_buffer;
	std::pmr::vector<std::pmr::vector<uint32_t>*> delay_released_index_buffer;

	HGEGraphics::Texture* default_texture;
} oval_cgpu_device_t;

void oval_log(void* user_data, ECGPULogSeverity severity, const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vprintf(fmt, args);
	va_end(args);
}

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

void oval_free_aligned(void* user_data, void* ptr, size_t alignment, const void* pool)
{
	aligned_malloced -= ptr ? _aligned_msize(ptr, alignment, 0) : 0;
	_aligned_free(ptr);
}

oval_device_t* oval_create_device(const oval_device_descriptor* device_descriptor)
{
	if (SDL_Init(SDL_INIT_VIDEO) < 0)
		return nullptr;

	SDL_Window* window = SDL_CreateWindow("oval", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, device_descriptor->width, device_descriptor->height, SDL_WINDOW_SHOWN | SDL_WINDOW_VULKAN);
	if (window == nullptr)
	{
		SDL_Quit();
		return nullptr;
	}

	auto memory_resource = new std::pmr::unsynchronized_pool_resource();
	oval_device_t super = { .descriptor = *device_descriptor, .device = CGPU_NULLPTR, .gfx_queue = CGPU_NULLPTR, .deltaTime = 0 };

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
	};

	device_cgpu->instance = cgpu_create_instance(&instance_desc);

	uint32_t adapters_count = 0;
	cgpu_enum_adapters(device_cgpu->instance, CGPU_NULLPTR, &adapters_count);
	CGPUAdapterId* adapters = (CGPUAdapterId*)_alloca(sizeof(CGPUAdapterId) * (adapters_count));
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
	device_cgpu->device = device_cgpu->super.device = cgpu_create_device(adapter, &device_desc);
	device_cgpu->gfx_queue = device_cgpu->super.gfx_queue = cgpu_get_queue(device_cgpu->device, CGPU_QUEUE_TYPE_GRAPHICS, 0);
	device_cgpu->present_queue = device_cgpu->gfx_queue;

	SDL_SysWMinfo wmInfo;
	SDL_VERSION(&wmInfo.version);
	SDL_GetWindowWMInfo(window, &wmInfo);
	device_cgpu->surface = cgpu_surface_from_native_view(device_cgpu->device, wmInfo.info.win.window);

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
		uint32_t colors[16];
		std::fill(colors, colors + 16, 0xffff00ff);
		device_cgpu->default_texture = oval_create_texture_from_buffer(&device_cgpu->super, u8"default black", 4, 4, (const unsigned char*)colors, sizeof(colors), false);
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

	unsigned char* fontPixels;
	int fontTexWidth, fontTexHeight;
	io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontTexWidth, &fontTexHeight);
	device_cgpu->imgui_font_texture = oval_create_texture_from_buffer(&device_cgpu->super, u8"ImGui Default Font Texture", fontTexWidth, fontTexHeight, fontPixels, fontTexWidth * fontTexHeight * 4, false);

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

void renderImgui(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::resource_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	if (device->imgui_draw_data)
	{
		auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
		uint32_t color = 0xffffffff;
		renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_LOAD, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
		renderpass_use_buffer(&passBuilder, device->imgui_mesh->dynamic_vertex_buffer_handle);
		renderpass_use_buffer(&passBuilder, device->imgui_mesh->dynamic_index_buffer_handle);

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

uint64_t uploadKTXTexture(HGEGraphics::rendergraph_t& rg, WaitUploadTexture& waited, HGEGraphics::resource_handle_t texture_handle);

uint64_t uploadTexture(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, std::pmr::vector<HGEGraphics::resource_handle_t>& uploaded_texture_handles, WaitUploadTexture& waited)
{
	uint64_t size = 0;
	if (waited.loader || waited.ktxTexture)
	{
		auto texture_handle = rendergraph_import_texture(&rg, waited.texture);
		if (waited.loader_type == 0 && waited.loader)
		{
			auto texture = waited.texture;
			auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max(size >> mip, 1ull); };
			const uint64_t xBlocksCount = mipedSize(texture->handle->info->width, 0) / FormatUtil_WidthOfBlock(texture->handle->info->format);
			const uint64_t yBlocksCount = mipedSize(texture->handle->info->height, 0) / FormatUtil_HeightOfBlock(texture->handle->info->format);
			size = xBlocksCount * yBlocksCount * FormatUtil_BitSizeOfBlock(texture->handle->info->format) / 8;
			rendergraph_add_uploadtexturepass_ex(&rg, u8"upload texture", texture_handle, 0, 0, size, 0, waited.loader, [](HGEGraphics::UploadEncoder* encoder, void* passdata){}, 0, nullptr);
		}
		else if (waited.loader_type == 1 && waited.ktxTexture)
		{
			size = uploadKTXTexture(rg, waited, texture_handle);
		}

		if (waited.texture->handle->info->mip_levels > 1 && waited.generate_mipmap)
			rendergraph_add_generate_mipmap(&rg, texture_handle);
		uploaded_texture_handles.push_back(texture_handle);

		if (waited.loader)
			device->delay_released_stbi_loader.push_back(waited.loader);
		if (waited.ktxTexture)
			device->delay_released_ktxTexture.push_back(waited.ktxTexture);
	}
	waited.texture->prepared = true;
	return size;
}

void uploadResources(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg)
{
	if (device->wait_upload_texture.empty() && device->wait_upload_mesh.empty())
		return;

	const uint32_t max_size = 1024 * 1024 * sizeof(uint32_t) * 10;
	uint32_t uploaded = 0;
	std::pmr::vector<HGEGraphics::resource_handle_t> uploaded_texture_handles(device->memory_resource);
	std::pmr::vector<HGEGraphics::buffer_handle_t> uploaded_buffer_handle(device->memory_resource);
	uploaded_texture_handles.reserve(device->wait_upload_texture.size());
	uploaded_buffer_handle.reserve(device->wait_upload_mesh.size() * 2);
	while (uploaded < max_size && !device->wait_upload_texture.empty())
	{
		auto waited = device->wait_upload_texture.front();
		device->wait_upload_texture.pop();
		uploaded += uploadTexture(device, rg, uploaded_texture_handles, waited);
	}

	while (uploaded < max_size && !device->wait_upload_mesh.empty())
	{
		auto waited = device->wait_upload_mesh.front();
		device->wait_upload_mesh.pop();

		auto mesh_vertex_handle = rendergraph_declare_buffer(&rg);
		rg_buffer_import(&rg, mesh_vertex_handle, waited.mesh->vertex_buffer);
		if (waited.vertex_data)
		{
			uint64_t size = waited.vertex_data->size() * sizeof(TexturedVertex);
			uploaded += size;
			struct UploadVertexPassData
			{
				std::pmr::vector<TexturedVertex>* vertex_data;
			};
			UploadVertexPassData* passdata;
			rendergraph_add_uploadbufferpass_ex(&rg, u8"upload mesh vertex data", mesh_vertex_handle, size, 0, waited.vertex_data->data(), [](HGEGraphics::UploadEncoder* encoder, void* passdata)
				{
					UploadVertexPassData* resolved_passdata = (UploadVertexPassData*)passdata;
					std::pmr::vector<TexturedVertex>* vertex_data = resolved_passdata->vertex_data;
					vertex_data->clear();
				}, sizeof(UploadVertexPassData), (void**)&passdata);
			passdata->vertex_data = waited.vertex_data;
			uploaded_buffer_handle.push_back(mesh_vertex_handle);

			device->delay_released_vertex_buffer.push_back(waited.vertex_data);
		}

		if (waited.mesh->index_buffer)
		{
			auto mesh_index_handle= rendergraph_declare_buffer(&rg);
			rg_buffer_import(&rg, mesh_index_handle, waited.mesh->index_buffer);
			if (waited.index_data)
			{
				uint64_t size = waited.index_data->size() * sizeof(uint32_t);
				uploaded += size;
				struct UploadIndexPassData
				{
					std::pmr::vector<uint32_t>* index_data;
				};
				UploadIndexPassData* passdata;
				rendergraph_add_uploadbufferpass_ex(&rg, u8"upload mesh index data", mesh_index_handle, size, 0, waited.index_data->data(), [](HGEGraphics::UploadEncoder* encoder, void* passdata)
					{
						UploadIndexPassData* resolved_passdata = (UploadIndexPassData*)passdata;
						std::pmr::vector<uint32_t>* index_data = resolved_passdata->index_data;
						index_data->clear();
					}, sizeof(UploadIndexPassData), (void**)&passdata);
				passdata->index_data = waited.index_data;
				uploaded_buffer_handle.push_back(mesh_index_handle);

				device->delay_released_index_buffer.push_back(waited.index_data);
			}
		}

		waited.mesh->prepared = true;
	}

	auto passBuilder = rendergraph_add_holdpass(&rg, u8"upload texture holdon");
	for (auto& handle : uploaded_texture_handles)
		renderpass_sample(&passBuilder, handle);
	uploaded_texture_handles.clear();

	for (auto& handle : uploaded_buffer_handle)
		renderpass_use_buffer(&passBuilder, handle);
	uploaded_buffer_handle.clear();
}

void release_uploader_data(oval_cgpu_device_t* device)
{
	for (auto loader: device->delay_released_stbi_loader)
		stbi_image_free(loader);
	device->delay_released_stbi_loader.clear();

	for (auto loader : device->delay_released_ktxTexture)
		ktxTexture_Destroy(loader);
	device->delay_released_ktxTexture.clear();

	for (auto loader : device->delay_released_vertex_buffer)
		delete loader;
	device->delay_released_vertex_buffer.clear();

	for (auto loader : device->delay_released_index_buffer)
		delete loader;
	device->delay_released_index_buffer.clear();
}

void render(oval_cgpu_device_t* device, HGEGraphics::Backbuffer* backbuffer)
{
	using namespace HGEGraphics;

	std::pmr::unsynchronized_pool_resource rg_pool(device->memory_resource);
	rendergraph_t rg{ 1, 1, 1, device->blit_shader, device->blit_linear_sampler, &rg_pool };

	uploadResources(device, rg);

	auto rg_back_buffer = rendergraph_import_backbuffer(&rg, backbuffer);

	auto imgui_mesh = setupImGuiResources(device, rg);

	if (device->super.descriptor.on_draw)
		device->super.descriptor.on_draw(&device->super, rg, rg_back_buffer);
	renderImgui(device, rg, rg_back_buffer);

	rendergraph_present(&rg, rg_back_buffer);

	auto compiled = Compiler::Compile(rg, &rg_pool);
	Executor::Execute(compiled, device->frameDatas[device->current_frame_index].execContext);

	dynamic_mesh_reset(device->imgui_mesh);

	release_uploader_data(device);
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

		if (D->rdc && D->rdc_capture)
			D->rdc->StartFrameCapture(nullptr, nullptr);

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

		if (D->rdc && D->rdc_capture)
		{
			D->rdc->EndFrameCapture(nullptr, nullptr);

			if (!D->rdc->IsRemoteAccessConnected())
			{
				D->rdc->LaunchReplayUI(1, "");
			}
		}
		D->rdc_capture = false;
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

	D->super.device = CGPU_NULLPTR;
	D->super.gfx_queue = CGPU_NULLPTR;
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

bool endsWithKtx(const char* str) {
	const char* suffix = ".ktx";
	size_t len = strlen(str);
	size_t suffixLen = strlen(suffix);
	if (len >= suffixLen) {
		return strncmp(str + len - suffixLen, suffix, suffixLen) == 0;
	}
	return false;
}

std::pair<ECGPUFormat, int> detectKtxTextureFormat(ktxTexture* ktxTexture)
{
	if (ktxTexture->classId == ktxTexture1_c)
	{
		auto ktx1 = (ktxTexture1*)ktxTexture;
		switch (ktx1->glInternalformat)
		{
		case 0x1908:
			return { CGPU_FORMAT_R8G8B8A8_UNORM, 4 };
		case 0x881A:
			return { CGPU_FORMAT_R16G16B16A16_SFLOAT, 8 };
		}
		printf("format: %d\n", ktx1->glFormat);
	}
	else if (ktxTexture->classId == ktxTexture2_c)
	{
		auto ktx2 = (ktxTexture2*)ktxTexture;
		switch (ktx2->vkFormat)
		{
		case 23:
			return { CGPU_FORMAT_R8G8B8A8_UNORM, 3 };
		}
		printf("format: %d\n", ktx2->vkFormat);
	}
	return { CGPU_FORMAT_UNDEFINED, 0 };
}

HGEGraphics::Texture* load_texture_ktx(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	ktxResult result = KTX_SUCCESS;
	ktxTexture* ktxTexture;
	result = ktxTexture_CreateFromNamedFile((const char*)filepath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
	if (result != KTX_SUCCESS)
		return nullptr;

	auto[format, component] = detectKtxTextureFormat(ktxTexture);
	// TODO: support compressed ktxTexture
	if (ktxTexture->isCompressed || format == CGPU_FORMAT_UNDEFINED)
	{
		ktxTexture_Destroy(ktxTexture);
		return nullptr;
	}

	uint32_t width = ktxTexture->baseWidth;
	uint32_t height = ktxTexture->baseHeight;
	uint32_t mipLevels = ktxTexture->numLevels;
	uint32_t arraySize = 1;

	bool generateMipmap = mipmap && mipLevels <= 1;
	mipLevels = mipmap ? (mipLevels > 1 ? mipLevels : (static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1)) : 1;
	CGPUResourceTypes descriptors = CGPU_RESOURCE_TYPE_TEXTURE;
	if (generateMipmap)
		descriptors |= CGPU_RESOURCE_TYPE_RENDER_TARGET;
	if (ktxTexture->isCubemap)
	{
		descriptors |= CGPU_RESOURCE_TYPE_TEXTURE_CUBE;
		arraySize = 6;
	}
	CGPUTextureDescriptor texture_desc =
	{
		.name = filepath,
		.width = (uint64_t)width,
		.height = (uint64_t)height,
		.depth = 1,
		.array_size = arraySize,
		.format = format,
		.mip_levels = mipLevels,
		.owner_queue = device->gfx_queue,
		.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		.descriptors = descriptors,
	};

	auto texture = HGEGraphics::create_texture(device->device, texture_desc);

	auto D = (oval_cgpu_device_t*)device;
	D->wait_upload_texture.push({ texture, 1, nullptr, ktxTexture, generateMipmap, component });

	return texture;

	ktxTexture_Destroy(ktxTexture);

	return nullptr;
}

HGEGraphics::Texture* load_texture_raw(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	int width = 0, height = 0, components = 0;
	auto texture_loader = stbi_load((const char*)filepath, &width, &height, &components, 4);
	if (!texture_loader)
		return nullptr;

	const char* filename = nullptr;
	if (filepath)
	{
		filename = strrchr((const char*)filepath, '/');
		if (!filename)
			filename = strrchr((const char*)filepath, '\\');
		filename = filename ? filename + 1 : (const char*)filepath;
	}

	auto mipLevels = mipmap ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
	CGPUTextureDescriptor texture_desc =
	{
		.name = (const char8_t*)filename,
		.width = (uint64_t)width,
		.height = (uint64_t)height,
		.depth = 1,
		.array_size = 1,
		.format = CGPU_FORMAT_R8G8B8A8_UNORM,
		.mip_levels = mipLevels,
		.owner_queue = device->gfx_queue,
		.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		.descriptors = CGPUResourceTypes(mipmap ? CGPU_RESOURCE_TYPE_TEXTURE | CGPU_RESOURCE_TYPE_RENDER_TARGET : CGPU_RESOURCE_TYPE_TEXTURE),
	};

	auto texture = HGEGraphics::create_texture(device->device, texture_desc);

	auto D = (oval_cgpu_device_t*)device;
	D->wait_upload_texture.push({ texture, 0, texture_loader, nullptr, mipmap && texture->handle->info->mip_levels > 1, 4 });

	return texture;
}


HGEGraphics::Texture* oval_load_texture(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	if (endsWithKtx((const char*)filepath))
		return load_texture_ktx(device, filepath, mipmap);
	else
		return load_texture_raw(device, filepath, mipmap);
}

HGEGraphics::Texture* oval_create_texture_from_buffer(oval_device_t* device, const char8_t* name, uint32_t width, uint32_t height, const unsigned char* data, uint64_t data_size, bool mipmap)
{
	auto mipLevels = mipmap ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
	CGPUTextureDescriptor texture_desc =
	{
		.name = name,
		.width = (uint64_t)width,
		.height = (uint64_t)height,
		.depth = 1,
		.array_size = 1,
		.format = CGPU_FORMAT_R8G8B8A8_UNORM,
		.mip_levels = mipLevels,
		.owner_queue = device->gfx_queue,
		.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		.descriptors = CGPUResourceTypes(mipmap ? CGPU_RESOURCE_TYPE_TEXTURE | CGPU_RESOURCE_TYPE_RENDER_TARGET : CGPU_RESOURCE_TYPE_TEXTURE),
	};
	auto texture = HGEGraphics::create_texture(device->device, texture_desc);

	stbi_uc* copy_data = (stbi_uc*)stbi__malloc(data_size);
	memcpy(copy_data, data, data_size);

	auto D = (oval_cgpu_device_t*)device;
	D->wait_upload_texture.push({ texture, 0, copy_data, nullptr, mipmap && texture->handle->info->mip_levels > 1 });

	return texture;
}

std::tuple<std::pmr::vector<TexturedVertex>*, std::pmr::vector<uint32_t>*> LoadObjModel(const char8_t* filename, bool right_hand, std::pmr::memory_resource* memory_resource)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	std::pmr::vector<TexturedVertex>* vertices;
	std::pmr::vector<uint32_t>* indices;
	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, (const char*)filename))
	{
		return { vertices, indices };
	}

	vertices = new std::pmr::vector<TexturedVertex>(memory_resource);
	indices = new std::pmr::vector<uint32_t>(memory_resource);

	std::pmr::vector<HMM_Vec3> coords(memory_resource), normals(memory_resource);
	std::pmr::vector<HMM_Vec2> texCoords(memory_resource);
	int rh = right_hand ? -1 : 1;

	coords.reserve(attrib.vertices.size() / 3);
	for (size_t i = 0; i < attrib.vertices.size() / 3; ++i)
	{
		coords.push_back({ attrib.vertices[3 * i + 0] * rh, attrib.vertices[3 * i + 1], attrib.vertices[3 * i + 2] });
	}

	normals.reserve(attrib.normals.size() / 3);
	for (size_t i = 0; i < attrib.normals.size() / 3; ++i)
	{
		normals.push_back({ attrib.normals[3 * i + 0] * rh, attrib.normals[3 * i + 1], attrib.normals[3 * i + 2] });
	}

	texCoords.reserve(attrib.texcoords.size() / 2);
	for (size_t i = 0; i < attrib.texcoords.size() / 2; ++i)
	{
		texCoords.push_back({ attrib.texcoords[2 * i + 0], 1 - attrib.texcoords[2 * i + 1] });
	}

	struct TripleIntHasher
	{
		size_t operator()(const std::tuple<int, int, int>& t) const {
			size_t h1 = std::hash<int>{}(get<0>(t));
			size_t h2 = std::hash<int>{}(get<1>(t));
			size_t h3 = std::hash<int>{}(get<2>(t));
			return ((h1 ^ (h2 << 1)) >> 1) ^ h3;
		}
	};
	std::unordered_map<std::tuple<int, int, int>, uint32_t, TripleIntHasher> vertex_map;

	vertices->reserve(coords.size());
	for (size_t i = 0; i < shapes.size(); ++i)
	{
		auto& mesh = shapes[i].mesh;
		indices->reserve(mesh.indices.size());
		for (size_t j = 0; j < mesh.indices.size(); ++j)
		{
			int vertex_index = mesh.indices[j].vertex_index;
			int normal_index = mesh.indices[j].normal_index;
			int texcoord_index = mesh.indices[j].texcoord_index;

			auto vertex_map_index = std::tuple{ vertex_index, normal_index, texcoord_index };
			auto iter = vertex_map.find(vertex_map_index);
			if (iter != vertex_map.end())
			{
				auto vertex_index = iter->second;
				indices->push_back(vertex_index);
			}
			else
			{
				auto pos = coords[vertex_index];
				auto normal = normal_index >= 0 ? normals[normal_index] : HMM_Vec3();
				auto texcoord = texcoord_index >= 0 ? texCoords[texcoord_index] : HMM_Vec2();
				auto iter = vertex_map.insert({ vertex_map_index , vertices->size() });
				vertices->push_back({ pos, normal, texcoord });
				indices->push_back(iter.first->second);
			}
		}

		break;
	}

	return { vertices, indices };
}

HGEGraphics::Mesh* oval_load_mesh(oval_device_t* device, const char8_t* filepath)
{
	auto D = (oval_cgpu_device_t*)device;
	auto [data, indices] = LoadObjModel(filepath, true, D->memory_resource);

	if (!data)
	{
		if (indices)
			delete indices;
		return nullptr;
	}

	CGPUVertexLayout mesh_vertex_layout =
	{
		.attribute_count = 3,
		.attributes =
		{
			{ u8"POSITION", 1, CGPU_FORMAT_R32G32B32_SFLOAT, 0, 0, sizeof(float) * 3, CGPU_INPUT_RATE_VERTEX },
			{ u8"NORMAL", 1, CGPU_FORMAT_R32G32B32_SFLOAT, 0, sizeof(float) * 3, sizeof(float) * 3, CGPU_INPUT_RATE_VERTEX },
			{ u8"TEXCOORD", 1, CGPU_FORMAT_R32G32_SFLOAT, 0, sizeof(float) * 6, sizeof(float) * 2, CGPU_INPUT_RATE_VERTEX },
		}
	};
	auto mesh = HGEGraphics::create_mesh(device->device, data->size(), (indices ? indices->size() : 0), CGPU_PRIM_TOPO_TRI_LIST, mesh_vertex_layout, (indices ? sizeof(uint32_t) : 0));

	D->wait_upload_mesh.push({ mesh, data, indices });

	return mesh;
}
