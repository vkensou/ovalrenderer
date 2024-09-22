#include "cgpu_device.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

HGEGraphics::Texture* oval_create_texture(oval_device_t* device, const CGPUTextureDescriptor& desc)
{
	auto D = (oval_cgpu_device_t*)device;
	return HGEGraphics::create_texture(D->device, desc);
}

void oval_free_texture(oval_device_t* device, HGEGraphics::Texture* texture)
{
	HGEGraphics::free_texture(texture);
}

HGEGraphics::Mesh* oval_create_mesh_from_buffer(oval_device_t* device, uint32_t vertex_count, uint32_t index_count, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout, uint32_t index_stride, const uint8_t* vertex_data, const uint8_t* index_data, bool update_vertex_data_from_compute_shader, bool update_index_data_from_compute_shader)
{
	auto D = (oval_cgpu_device_t*)device;
	auto mesh = HGEGraphics::create_mesh(D->device, vertex_count, index_count, prim_topology, vertex_layout, index_stride, update_vertex_data_from_compute_shader, update_index_data_from_compute_shader);

	auto vertex_raw_data = malloc(vertex_count * mesh->vertex_stride);
	memcpy(vertex_raw_data, vertex_data, vertex_count * mesh->vertex_stride);
	void* index_raw_data = nullptr;
	if (index_data)
	{
		index_raw_data = malloc(index_count * mesh->index_stride);
		memcpy(index_raw_data, index_data, index_count * mesh->index_stride);
	}

	//D->wait_upload_mesh.push({ mesh, nullptr, nullptr, vertex_raw_data, index_raw_data, mesh->vertices_count * mesh->vertex_stride, mesh->index_count * mesh->index_stride });

	return mesh;
}

void oval_free_mesh(oval_device_t* device, HGEGraphics::Mesh* mesh)
{
	HGEGraphics::free_mesh(mesh);
}

HGEGraphics::Shader* oval_create_shader(oval_device_t* device, const std::string& vertPath, const std::string& fragPath, const CGPUBlendStateDescriptor& blend_desc, const CGPUDepthStateDesc& depth_desc, const CGPURasterizerStateDescriptor& rasterizer_state)
{
	auto D = (oval_cgpu_device_t*)device;
	return HGEGraphics::create_shader(D->device, vertPath, fragPath, blend_desc, depth_desc, rasterizer_state);
}

void oval_free_shader(oval_device_t* device, HGEGraphics::Shader* shader)
{
	HGEGraphics::free_shader(shader);
}

HGEGraphics::ComputeShader* oval_create_compute_shader(oval_device_t* device, const std::string& compPath)
{
	auto D = (oval_cgpu_device_t*)device;
	return HGEGraphics::create_compute_shader(D->device, compPath);
}

void oval_free_compute_shader(oval_device_t* device, HGEGraphics::ComputeShader* shader)
{
	HGEGraphics::free_compute_shader(shader);
}

CGPUSamplerId oval_create_sampler(oval_device_t* device, const CGPUSamplerDescriptor* desc)
{
	auto D = (oval_cgpu_device_t*)device;
	return cgpu_create_sampler(D->device, desc);
}

void oval_free_sampler(oval_device_t* device, CGPUSamplerId sampler)
{
	cgpu_free_sampler(sampler);
}

bool oval_texture_prepared(oval_device_t* device, HGEGraphics::Texture* texture)
{
	return texture->prepared;
}

bool oval_mesh_prepared(oval_device_t* device, HGEGraphics::Mesh* mesh)
{
	return mesh->prepared;
}

HGEGraphics::Buffer* oval_mesh_get_vertex_buffer(oval_device_t* device, HGEGraphics::Mesh* mesh)
{
	return mesh->vertex_buffer;
}

HGEGraphics::Texture* oval_load_texture(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	auto D = (oval_cgpu_device_t*)device;

	WaitLoadResource resource;
	resource.type = WaitLoadResourceType::Texture;
	size_t path_size = strlen((const char*)filepath) + 1;
	char8_t* path = (char8_t*)D->allocator.allocate_bytes(path_size);
	memcpy(path, filepath, path_size);
	resource.path = path;
	resource.path_size = path_size;
	resource.textureResource = {
		.texture = HGEGraphics::create_empty_texture(),
		.mipmap = mipmap,
	};
	resource.textureResource.texture->prepared = false;
	D->wait_load_resources.push(resource);
	return resource.textureResource.texture;
}

HGEGraphics::Mesh* oval_load_mesh(oval_device_t* device, const char8_t* filepath)
{
	auto D = (oval_cgpu_device_t*)device;

	WaitLoadResource resource;
	resource.type = WaitLoadResourceType::Mesh;
	size_t path_size = strlen((const char*)filepath) + 1;
	char8_t* path = (char8_t*)D->allocator.allocate_bytes(path_size);
	memcpy(path, filepath, path_size);
	resource.path = path;
	resource.path_size = path_size;
	resource.meshResource = {
		.mesh = HGEGraphics::create_empty_mesh(),
	};
	resource.meshResource.mesh->prepared = false;
	D->wait_load_resources.push(resource);
	return resource.meshResource.mesh;
}

void oval_process_load_queue(oval_cgpu_device_t* device)
{
	if (device->wait_load_resources.empty())
		return;

	auto queue = oval_graphics_transfer_queue_alloc(&device->super);

	const uint64_t max_size = 1024 * 1024 * sizeof(uint32_t) * 10;
	uint64_t uploaded = 0;
	while (uploaded < max_size && !device->wait_load_resources.empty())
	{
		auto waited = device->wait_load_resources.front();
		device->wait_load_resources.pop();
		if (waited.type == WaitLoadResourceType::Texture)
		{
			auto& textureResource = waited.textureResource;
			uploaded += load_texture(device, queue, textureResource.texture, waited.path, textureResource.mipmap);
			waited.textureResource.texture->prepared = true;
			device->allocator.deallocate_bytes((void*)waited.path, waited.path_size);
		}
		else if (waited.type == WaitLoadResourceType::Mesh)
		{
			auto& meshResource = waited.meshResource;
			uploaded += load_mesh(device, queue, meshResource.mesh, waited.path);
			waited.meshResource.mesh->prepared = true;
			device->allocator.deallocate_bytes((void*)waited.path, waited.path_size);
		}
	}

	oval_graphics_transfer_queue_submit(&device->super, queue);
}

void oval_ensure_cur_transfer_queue(oval_cgpu_device_t* device)
{
	if (device->cur_transfer_queue != nullptr)
		return;

	device->cur_transfer_queue = oval_graphics_transfer_queue_alloc(&device->super);
}

uint8_t* oval_graphics_set_texture_data_slice(oval_device_t* device, HGEGraphics::Texture* texture, uint32_t mipmap, uint32_t slice, uint64_t* size)
{
	auto D = (oval_cgpu_device_t*)device;

	oval_ensure_cur_transfer_queue(D);

	return oval_graphics_transfer_queue_transfer_data_to_texture_slice(D->cur_transfer_queue, texture, mipmap, slice, size);
}
