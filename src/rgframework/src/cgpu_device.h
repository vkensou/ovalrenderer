#pragma once

#include "framework.h"
#include <SDL.h>
#include "imgui.h"
#include "renderdoc_helper.h"
#include <queue>
#include "ktx.h"
#include "stb_image.h"
#include "renderer.h"

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

struct TexturedVertex
{
	HMM_Vec3 position;
	HMM_Vec3 normal;
	HMM_Vec2 texCoord;
};


struct WaitUploadMesh
{
	HGEGraphics::Mesh* mesh;
	std::pmr::vector<TexturedVertex>* vertex_data;
	std::pmr::vector<uint32_t>* index_data;
	void* vertex_raw_data;
	void* index_raw_data;
	uint64_t vertex_data_size;
	uint64_t index_data_size;
};

typedef struct oval_cgpu_device_t {
	oval_cgpu_device_t(const oval_device_t& super, std::pmr::memory_resource* memory_resource)
		: super(super), memory_resource(memory_resource), wait_upload_texture(memory_resource), wait_upload_mesh(memory_resource), delay_released_stbi_loader(memory_resource), delay_released_ktxTexture(memory_resource), delay_released_vertex_buffer(memory_resource), delay_released_index_buffer(memory_resource), delay_freeed_raw_data(memory_resource)
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
	std::pmr::vector<void*> delay_freeed_raw_data;

	HGEGraphics::Texture* default_texture;
} oval_cgpu_device_t;
