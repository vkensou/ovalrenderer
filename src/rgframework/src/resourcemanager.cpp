﻿#include "cgpu_device.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

HGEGraphics::Texture* oval_create_texture(oval_device_t* device, const CGPUTextureDescriptor& desc)
{
	auto D = (oval_cgpu_device_t*)device;
	return HGEGraphics::create_texture(D->device, desc);
}

HGEGraphics::Texture* oval_create_texture_from_buffer(oval_device_t* device, const CGPUTextureDescriptor& desc, void* data, uint64_t size)
{
	auto D = (oval_cgpu_device_t*)device;
	auto texture = oval_create_texture(device, desc);
	uint64_t used_size = 0;
	auto colors = (uint32_t*)oval_graphics_set_texture_data_slice(device, texture, 0, 0, &used_size);
	assert(used_size == size);
	memcpy(colors, data, used_size);
	return texture;
}

void oval_free_texture(oval_device_t* device, HGEGraphics::Texture* texture)
{
	HGEGraphics::free_texture(texture);
}

HGEGraphics::Mesh* oval_create_mesh_from_buffer(oval_device_t* device, uint32_t vertex_count, uint32_t index_count, ECGPUPrimitiveTopology prim_topology, const CGPUVertexLayout& vertex_layout, uint32_t index_stride, const uint8_t* vertex_data, const uint8_t* index_data, bool update_vertex_data_from_compute_shader, bool update_index_data_from_compute_shader)
{
	auto D = (oval_cgpu_device_t*)device;
	auto mesh = HGEGraphics::create_mesh(D->device, vertex_count, index_count, prim_topology, vertex_layout, index_stride, update_vertex_data_from_compute_shader, update_index_data_from_compute_shader);

	auto upload_vertex_data = oval_graphics_set_mesh_vertex_data(device, mesh, nullptr);
	memcpy(upload_vertex_data, vertex_data, vertex_count * mesh->vertex_stride);
	if (index_data)
	{
		auto upload_index_data = oval_graphics_set_mesh_index_data(device, mesh, nullptr);
		memcpy(upload_index_data, index_data, index_count * mesh->index_stride);
	}
	mesh->prepared = true;
	return mesh;
}

void oval_free_mesh(oval_device_t* device, HGEGraphics::Mesh* mesh)
{
	HGEGraphics::free_mesh(mesh);
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

uint8_t* oval_graphics_set_mesh_vertex_data(oval_device_t* device, HGEGraphics::Mesh* mesh, uint64_t* size)
{
	auto D = (oval_cgpu_device_t*)device;

	oval_ensure_cur_transfer_queue(D);

	uint64_t vertex_data_size = mesh->vertices_count * mesh->vertex_stride;
	if (size)
		*size = vertex_data_size;
	return oval_graphics_transfer_queue_transfer_data_to_buffer(D->cur_transfer_queue, vertex_data_size, mesh->vertex_buffer);
}

uint8_t* oval_graphics_set_mesh_index_data(oval_device_t* device, HGEGraphics::Mesh* mesh, uint64_t* size)
{
	uint64_t index_data_size = mesh->index_count * mesh->index_stride;
	if (index_data_size == 0)
		return nullptr;

	auto D = (oval_cgpu_device_t*)device;

	oval_ensure_cur_transfer_queue(D);

	if (size)
		*size = index_data_size;
	return oval_graphics_transfer_queue_transfer_data_to_buffer(D->cur_transfer_queue, index_data_size, mesh->index_buffer);
}

uint8_t* oval_graphics_set_texture_data_slice(oval_device_t* device, HGEGraphics::Texture* texture, uint32_t mipmap, uint32_t slice, uint64_t* size)
{
	auto D = (oval_cgpu_device_t*)device;

	oval_ensure_cur_transfer_queue(D);
	texture->prepared = true;	// TODO: 这里应该设置吗
	return oval_graphics_transfer_queue_transfer_data_to_texture_slice(D->cur_transfer_queue, texture, mipmap, slice, size);
}

std::vector<uint8_t> readfile(const char8_t* filename)
{
    SDL_RWops *rw = SDL_RWFromFile((const char*)filename, "rb");
	if (!rw)
	{
		throw std::runtime_error("failed to open file!");
	}

    auto size = SDL_RWsize(rw);

    std::vector<uint8_t> buffer(size);
    SDL_RWread(rw, buffer.data(), sizeof(char), size);
    // don't forget to close the file
    SDL_RWclose(rw);
	return buffer;
}
