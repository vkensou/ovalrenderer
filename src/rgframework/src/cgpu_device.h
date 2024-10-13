#pragma once

#include "framework.h"
#include <SDL.h>
#include "imgui.h"
#include "renderdoc_helper.h"
#include <queue>
#include "renderer.h"
#include "tbox/tbox.h"

struct oval_transfer_data_to_texture
{
	HGEGraphics::Texture* texture;
	uint8_t* data;
	uint64_t size;
	uint32_t mipmap;
	uint32_t slice;
	bool transfer_full;
	bool generate_mipmap;
	uint8_t generate_mipmap_from;
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

class tbox_memory_resource : public std::pmr::memory_resource
{
private:
	tb_allocator_ref_t upstream;

public:
	explicit tbox_memory_resource(tb_allocator_ref_t upstream) : upstream(upstream) {
	}

private:
	void* do_allocate(size_t bytes, size_t alignment) override {
		return tb_allocator_align_malloc(upstream, bytes, alignment);
	}
	void do_deallocate(void* ptr, size_t bytes, size_t alignment) override {
		tb_allocator_align_free(upstream, ptr);
	}
	bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
		if (this == &other)
			return true;

		auto op = dynamic_cast<const tbox_memory_resource*>(&other);
		return op != nullptr;
	}
};

typedef struct oval_cgpu_device_t {
	oval_cgpu_device_t(const oval_device_t& super, tb_allocator_ref_t tb_allocator, std::pmr::memory_resource* memory_resource)
		: super(super), tb_allocator(tb_allocator), memory_resource(memory_resource), transfer_queue(memory_resource), allocator(memory_resource), wait_load_resources(memory_resource)
	{
	}

	oval_device_t super;
	SDL_Window* window;
	tb_allocator_ref_t tb_allocator;
	std::pmr::memory_resource* memory_resource;
	std::pmr::polymorphic_allocator<std::byte> allocator;
	CGPUInstanceId instance;
	CGPUDeviceId device;
	CGPUQueueId gfx_queue;
	CGPUQueueId present_queue;

	CGPUSurfaceId surface;
	CGPUSwapChainId swapchain;
	std::vector<HGEGraphics::Backbuffer> backbuffer;
	std::vector<CGPUSemaphoreId> swapchain_prepared_semaphores;

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
std::vector<uint8_t> readfile(const char8_t* filename);