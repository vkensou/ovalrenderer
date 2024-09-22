#pragma once

#include "framework.h"
#include <SDL.h>
#include "imgui.h"
#include "renderdoc_helper.h"
#include <queue>
#include "ktx.h"
#include "stb_image.h"
#include "renderer.h"

struct oval_transfer_data_to_texture
{
	HGEGraphics::Texture* texture;
	uint8_t* data;
	uint64_t size;
	uint32_t mipmap;
	uint32_t slice;
	bool transfer_full;
	bool generate_mipmap;
};

struct oval_transfer_data_to_buffer
{
	HGEGraphics::Buffer* buffer;
	uint8_t* data;
	uint64_t size;
};

struct oval_graphics_transfer_queue
{
	oval_graphics_transfer_queue(std::pmr::memory_resource* memory_resource)
		: textures(memory_resource), buffers(memory_resource), memory_resource(memory_resource)
	{
	}

	std::pmr::monotonic_buffer_resource memory_resource;
	std::pmr::vector<oval_transfer_data_to_texture> textures;
	std::pmr::vector<oval_transfer_data_to_buffer> buffers;
};

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

enum class WaitLoadResourceType
{
	Texture,
	Mesh,
};

struct WaitLoadResource
{
	WaitLoadResourceType type;
	const char8_t* path;
	size_t path_size;
	union {
		struct {
			HGEGraphics::Texture* texture;
			bool mipmap;
		} textureResource;
		struct {
			HGEGraphics::Mesh* mesh;
		} meshResource;
	};
};

struct TexturedVertex
{
	HMM_Vec3 position;
	HMM_Vec3 normal;
	HMM_Vec2 texCoord;
};

typedef struct oval_cgpu_device_t {
	oval_cgpu_device_t(const oval_device_t& super, std::pmr::memory_resource* memory_resource)
		: super(super), memory_resource(memory_resource), transfer_queue(memory_resource), allocator(memory_resource), wait_load_resources(memory_resource)
	{
	}

	oval_device_t super;
	SDL_Window* window;
	std::pmr::memory_resource* memory_resource;
	std::pmr::polymorphic_allocator<std::byte> allocator;
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

	std::pmr::vector<oval_graphics_transfer_queue*> transfer_queue;
	std::queue<WaitLoadResource, std::pmr::deque<WaitLoadResource>> wait_load_resources;
	oval_graphics_transfer_queue* cur_transfer_queue = nullptr;

	HGEGraphics::Texture* default_texture;
} oval_cgpu_device_t;

void oval_process_load_queue(oval_cgpu_device_t* device);
void oval_graphics_transfer_queue_execute_all(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg);
void oval_graphics_transfer_queue_release_all(oval_cgpu_device_t* device);
uint64_t load_mesh(oval_cgpu_device_t* device, oval_graphics_transfer_queue_t queue, HGEGraphics::Mesh* mesh, const char8_t* filepath);
uint64_t load_texture(oval_cgpu_device_t* device, oval_graphics_transfer_queue_t queue, HGEGraphics::Texture* texture, const char8_t* filepath, bool mipmap);
