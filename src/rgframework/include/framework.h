#pragma once

#include "stdint.h"
#include "rendergraph.h"
#include "drawer.h"
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
    float deltaTime;
} oval_device_t;

typedef struct oval_graphics_transfer_queue* oval_graphics_transfer_queue_t;

oval_device_t* oval_create_device(const oval_device_descriptor* device_descriptor);
void oval_runloop(oval_device_t* device);
void oval_free_device(oval_device_t* device);
void oval_render_debug_capture(oval_device_t* device);
void oval_query_render_profile(oval_device_t* device, uint32_t* length, const char8_t*** names, const float** durations);

HGEGraphics::Texture* oval_create_texture(oval_device_t* device, const CGPUTextureDescriptor& desc);
HGEGraphics::Texture* oval_load_texture(oval_device_t* device, const char8_t* filepath, bool mipmap);
void oval_free_texture(oval_device_t* device, HGEGraphics::Texture* texture);
HGEGraphics::Mesh* oval_load_mesh(oval_device_t* device, const char8_t* filepath);
HGEGraphics::Mesh* oval_create_mesh_from_buffer(oval_device_t* device, uint32_t vertex_count, uint32_t index_count, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout, uint32_t index_stride, const uint8_t* vertex_data, const uint8_t* index_data, bool update_vertex_data_from_compute_shader, bool update_index_data_from_compute_shader);
void oval_free_mesh(oval_device_t* device, HGEGraphics::Mesh* mesh);
HGEGraphics::Shader* oval_create_shader(oval_device_t* device, const std::string& vertPath, const std::string& fragPath, const CGPUBlendStateDescriptor& blend_desc, const CGPUDepthStateDesc& depth_desc, const CGPURasterizerStateDescriptor& rasterizer_state);
void oval_free_shader(oval_device_t* device, HGEGraphics::Shader* shader);
HGEGraphics::ComputeShader* oval_create_compute_shader(oval_device_t* device, const std::string& compPath);
void oval_free_compute_shader(oval_device_t* device, HGEGraphics::ComputeShader* shader);
CGPUSamplerId oval_create_sampler(oval_device_t* device, const struct CGPUSamplerDescriptor* desc);
void oval_free_sampler(oval_device_t* device, CGPUSamplerId sampler);
bool oval_texture_prepared(oval_device_t* device, HGEGraphics::Texture* texture);
bool oval_mesh_prepared(oval_device_t* device, HGEGraphics::Mesh* mesh);
HGEGraphics::Buffer* oval_mesh_get_vertex_buffer(oval_device_t* device, HGEGraphics::Mesh* mesh);
oval_graphics_transfer_queue_t oval_graphics_transfer_queue_alloc(oval_device_t* device);
void oval_graphics_transfer_queue_submit(oval_device_t* device, oval_graphics_transfer_queue_t queue);
uint8_t* oval_graphics_transfer_queue_transfer_data_to_buffer(oval_graphics_transfer_queue_t queue, uint64_t size, HGEGraphics::Buffer* buffer);
uint8_t* oval_graphics_transfer_queue_transfer_data_to_texture(oval_graphics_transfer_queue_t queue, uint64_t size, HGEGraphics::Texture* texture, bool generate_mipmap);
