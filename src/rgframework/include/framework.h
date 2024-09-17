#pragma once

#include "stdint.h"
#include "rendergraph.h"
#include "renderer.h"
#include "HandmadeMath.h"

typedef void (*oval_on_draw)(struct oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::texture_handle_t rg_back_buffer);
typedef void (*oval_on_update)(struct oval_device_t* device);
typedef void (*oval_on_imgui)(struct oval_device_t* device);

typedef struct oval_device_descriptor
{
    void* userdata;
    oval_on_update on_update;
    oval_on_imgui on_imgui;
    oval_on_draw on_draw;
    uint16_t width;
    uint16_t height;
    bool enable_capture;
    bool enable_profile;
} oval_device_descriptor;

typedef struct oval_device_t {
    const oval_device_descriptor descriptor;
    CGPUDeviceId device;
    CGPUQueueId gfx_queue;
    float deltaTime;
} oval_device_t;

struct TexturedVertex
{
    HMM_Vec3 position;
    HMM_Vec3 normal;
    HMM_Vec2 texCoord;
};

oval_device_t* oval_create_device(const oval_device_descriptor* device_descriptor);
void oval_runloop(oval_device_t* device);
void oval_free_device(oval_device_t* device);
void oval_render_debug_capture(oval_device_t* device);
void oval_query_render_profile(oval_device_t* device, uint32_t* length, const char8_t*** names, const float** durations);

HGEGraphics::Texture* oval_load_texture(oval_device_t* device, const char8_t* filepath, bool mipmap);
HGEGraphics::Texture* oval_create_texture_from_buffer(oval_device_t* device, const char8_t* name, uint32_t width, uint32_t height, const unsigned char* data, uint64_t data_size, bool mipmap);
HGEGraphics::Texture* oval_create_texture_from_buffer(oval_device_t* device, CGPUTextureDescriptor descriptor, const unsigned char* data, uint64_t data_size);
HGEGraphics::Mesh* oval_load_mesh(oval_device_t* device, const char8_t* filepath);
HGEGraphics::Mesh* oval_create_mesh_from_buffer(oval_device_t* device, uint32_t vertex_count, uint32_t index_count, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout, uint32_t index_stride, const uint8_t* vertex_data, const uint8_t* index_data, bool update_vertex_data_from_compute_shader, bool update_index_data_from_compute_shader);