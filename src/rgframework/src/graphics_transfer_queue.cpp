#include "cgpu_device.h"
#include "rendergraph.h"
#include "rendergraph_compiler.h"
#include "rendergraph_executor.h"
#include <cassert>

oval_graphics_transfer_queue_t oval_graphics_transfer_queue_alloc(oval_device_t* device)
{
	auto D = (oval_cgpu_device_t*)device;

	auto queue = D->allocator.new_object<oval_graphics_transfer_queue>(D->memory_resource);

	return queue;
}

void oval_graphics_transfer_queue_submit(oval_device_t* device, oval_graphics_transfer_queue_t queue)
{
	auto D = (oval_cgpu_device_t*)device;
	D->transfer_queue.push(queue);
}

void oval_graphics_transfer_queue_transfer_data_to_buffer(oval_graphics_transfer_queue_t queue, void* data, uint64_t size, HGEGraphics::Buffer* buffer)
{
	
}

uint8_t* oval_graphics_transfer_queue_transfer_data_to_texture(oval_graphics_transfer_queue_t queue, uint64_t size, HGEGraphics::Texture* texture, bool generate_mipmap)
{
	assert(size > 0);
	assert(texture != nullptr);
	uint8_t* data = (uint8_t*)queue->memory_resource.allocate(size);
	assert(data != nullptr);
	queue->textures.push({ texture, data, size, generate_mipmap });
	return data;
}

void uploadTexture(HGEGraphics::rendergraph_t& rg, std::pmr::vector<HGEGraphics::texture_handle_t>& uploaded_texture_handles, oval_transfer_data_to_texture& waited)
{
	auto texture_handle = rendergraph_import_texture(&rg, waited.texture);

	auto data = waited.data;
	for (size_t mipmap = 0; mipmap < (waited.generate_mipmap ? 1 : waited.texture->handle->info->mip_levels); ++mipmap)
	{
		for (size_t slice = 0; slice < waited.texture->handle->info->array_size_minus_one + 1; ++slice)
		{
			auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max(size >> mip, 1ull); };
			const uint64_t xBlocksCount = mipedSize(waited.texture->handle->info->width, mipmap) / FormatUtil_WidthOfBlock(waited.texture->handle->info->format);
			const uint64_t yBlocksCount = mipedSize(waited.texture->handle->info->height, mipmap) / FormatUtil_HeightOfBlock(waited.texture->handle->info->format);
			const uint64_t zBlocksCount = mipedSize(waited.texture->handle->info->depth, mipmap);
			uint64_t size = xBlocksCount * yBlocksCount * zBlocksCount * FormatUtil_BitSizeOfBlock(waited.texture->handle->info->format) / 8;
			uint64_t offset = 0;
			rendergraph_add_uploadtexturepass_ex(&rg, u8"upload texture", texture_handle, mipmap, slice, size, offset, data, [](HGEGraphics::UploadEncoder* encoder, void* passdata) {}, 0, nullptr);
			data += size;
		}
	}

	if (waited.texture->handle->info->mip_levels > 1 && waited.generate_mipmap)
		rendergraph_add_generate_mipmap(&rg, texture_handle);
	uploaded_texture_handles.push_back(texture_handle);
}

void oval_graphics_transfer_queue_execute(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, oval_graphics_transfer_queue_t queue)
{
	using namespace HGEGraphics;

	if (queue->textures.empty())
		return;

	const uint32_t max_size = 1024 * 1024 * sizeof(uint32_t) * 10;
	std::pmr::vector<HGEGraphics::texture_handle_t> uploaded_texture_handles(&queue->memory_resource);
	std::pmr::vector<HGEGraphics::buffer_handle_t> uploaded_buffer_handle(&queue->memory_resource);
	uploaded_texture_handles.reserve(queue->textures.size());
	//uploaded_buffer_handle.reserve(device->wait_upload_mesh.size() * 2);
	while (!queue->textures.empty())
	{
		auto waited = queue->textures.front();
		queue->textures.pop();
		uploadTexture(rg, uploaded_texture_handles, waited);
	}

	//while (uploaded < max_size && !device->wait_upload_mesh.empty())
	//{
	//	auto waited = device->wait_upload_mesh.front();
	//	device->wait_upload_mesh.pop();
	//	uploaded += uploadMesh(device, rg, uploaded_buffer_handle, waited);
	//}

	auto passBuilder = rendergraph_add_holdpass(&rg, u8"upload queue holdon");
	for (auto& handle : uploaded_texture_handles)
		renderpass_sample(&passBuilder, handle);
	uploaded_texture_handles.clear();

	for (auto& handle : uploaded_buffer_handle)
		renderpass_use_buffer(&passBuilder, handle);
	uploaded_buffer_handle.clear();
}

void oval_graphics_transfer_queue_execute_all(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg)
{
	while (!device->transfer_queue.empty())
	{
		auto queue = device->transfer_queue.front();
		device->transfer_queue.pop();
		oval_graphics_transfer_queue_execute(device, rg, queue);
		device->allocator.delete_object(queue);
	}
}