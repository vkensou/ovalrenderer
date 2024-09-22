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
	D->transfer_queue.push_back(queue);
}

uint8_t* oval_graphics_transfer_queue_transfer_data_to_buffer(oval_graphics_transfer_queue_t queue, uint64_t size, HGEGraphics::Buffer* buffer)
{
	assert(size > 0);
	assert(buffer != nullptr);
	uint8_t* data = (uint8_t*)queue->memory_resource.allocate(size);
	assert(data != nullptr);
	queue->buffers.emplace_back(buffer, data, size);
	return data;
}

uint8_t* oval_graphics_transfer_queue_transfer_data_to_texture_full(oval_graphics_transfer_queue_t queue, HGEGraphics::Texture* texture, bool generate_mipmap, uint8_t generate_mipmap_from, uint64_t* size)
{
	assert(texture != nullptr);

	auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max(size >> mip, 1ull); };

	uint64_t used_size = 0;
	size_t maxMipmap = generate_mipmap ? std::min((uint32_t)generate_mipmap_from, texture->handle->info->mip_levels) : texture->handle->info->mip_levels;
	for (size_t mipmap = 0; mipmap < maxMipmap; ++mipmap)
	{
		const uint64_t xBlocksCount = mipedSize(texture->handle->info->width, mipmap) / FormatUtil_WidthOfBlock(texture->handle->info->format);
		const uint64_t yBlocksCount = mipedSize(texture->handle->info->height, mipmap) / FormatUtil_HeightOfBlock(texture->handle->info->format);
		const uint64_t zBlocksCount = mipedSize(texture->handle->info->depth, mipmap);
		used_size += xBlocksCount * yBlocksCount * zBlocksCount * (texture->handle->info->array_size_minus_one + 1) * FormatUtil_BitSizeOfBlock(texture->handle->info->format) / 8;
	}

	uint8_t* data = (uint8_t*)queue->memory_resource.allocate(used_size);
	assert(data != nullptr);
	queue->textures.emplace_back(texture, data, used_size, 0, 0, true, generate_mipmap, generate_mipmap_from);
	if (size)
		*size = used_size;
	return data;
}

uint8_t* oval_graphics_transfer_queue_transfer_data_to_texture_slice(oval_graphics_transfer_queue_t queue, HGEGraphics::Texture* texture, uint32_t mipmap, uint32_t slice, uint64_t* size)
{
	assert(texture != nullptr);

	auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max(size >> mip, 1ull); };

	const uint64_t xBlocksCount = mipedSize(texture->handle->info->width, mipmap) / FormatUtil_WidthOfBlock(texture->handle->info->format);
	const uint64_t yBlocksCount = mipedSize(texture->handle->info->height, mipmap) / FormatUtil_HeightOfBlock(texture->handle->info->format);
	const uint64_t zBlocksCount = mipedSize(texture->handle->info->depth, mipmap);
	uint64_t used_size = xBlocksCount * yBlocksCount * zBlocksCount * FormatUtil_BitSizeOfBlock(texture->handle->info->format) / 8;

	uint8_t* data = (uint8_t*)queue->memory_resource.allocate(used_size);
	assert(data != nullptr);
	queue->textures.emplace_back(texture, data, used_size, mipmap, slice, false, false);
	if (size)
		*size = used_size;
	return data;
}

void uploadBuffer(HGEGraphics::rendergraph_t& rg, std::pmr::vector<HGEGraphics::buffer_handle_t>& uploaded_buffer_handles, oval_transfer_data_to_buffer& waited)
{
	auto buffer_handle = rendergraph_import_buffer(&rg, waited.buffer);
	uint64_t size = waited.size;
	rendergraph_add_uploadbufferpass_ex(&rg, u8"upload buffer", buffer_handle, size, 0, waited.data, nullptr, 0, nullptr);
	uploaded_buffer_handles.push_back(buffer_handle);
}

void uploadTexture(HGEGraphics::rendergraph_t& rg, std::pmr::vector<HGEGraphics::texture_handle_t>& uploaded_texture_handles, oval_transfer_data_to_texture& waited)
{
	auto texture_handle = rendergraph_import_texture(&rg, waited.texture);

	if (waited.transfer_full)
	{
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
	}
	else
	{
		rendergraph_add_uploadtexturepass_ex(&rg, u8"upload texture", texture_handle, waited.mipmap, waited.slice, waited.size, 0, waited.data, [](HGEGraphics::UploadEncoder* encoder, void* passdata) {}, 0, nullptr);
	}

	if (waited.texture->handle->info->mip_levels > 1 && waited.generate_mipmap)
		rendergraph_add_generate_mipmap(&rg, texture_handle, waited.generate_mipmap_from);
	uploaded_texture_handles.push_back(texture_handle);
}

void oval_graphics_transfer_queue_execute(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, oval_graphics_transfer_queue_t queue)
{
	using namespace HGEGraphics;

	if (queue->textures.empty() && queue->buffers.empty())
		return;

	const uint32_t max_size = 1024 * 1024 * sizeof(uint32_t) * 10;
	std::pmr::vector<HGEGraphics::texture_handle_t> uploaded_texture_handles(&queue->memory_resource);
	std::pmr::vector<HGEGraphics::buffer_handle_t> uploaded_buffer_handle(&queue->memory_resource);
	uploaded_texture_handles.reserve(queue->textures.size());
	uploaded_buffer_handle.reserve(queue->buffers.size());
	for (auto& waited : queue->textures)
	{
		uploadTexture(rg, uploaded_texture_handles, waited);
	}

	for (auto& waited : queue->buffers)
	{
		uploadBuffer(rg, uploaded_buffer_handle, waited);
	}

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
	for (auto& queue : device->transfer_queue)
	{
		oval_graphics_transfer_queue_execute(device, rg, queue);
	}
}

void oval_graphics_transfer_queue_release_all(oval_cgpu_device_t* device)
{
	for (auto& queue : device->transfer_queue)
	{
		for (auto& waited : queue->textures)
		{
			queue->memory_resource.deallocate(waited.data, waited.size);
		}
		queue->textures.clear();
		for (auto& waited : queue->buffers)
		{
			queue->memory_resource.deallocate(waited.data, waited.size);
		}
		queue->buffers.clear();
		device->allocator.delete_object(queue);
	}
	device->transfer_queue.clear();
}
