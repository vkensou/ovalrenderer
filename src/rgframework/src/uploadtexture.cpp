#include "renderer.h"
#include "rendergraph.h"
#include "ktx.h"
#include <cassert>
#include "drawer.h"
#include "uploadresource.h"

typedef unsigned char stbi_uc;

uint64_t uploadKTXTexture(HGEGraphics::rendergraph_t& rg, WaitUploadTexture& waited, HGEGraphics::texture_handle_t texture_handle)
{
	auto uploadMip = [](HGEGraphics::rendergraph_t& rg, WaitUploadTexture& waited, HGEGraphics::texture_handle_t texture_handle, uint8_t mipmap, uint8_t slice, uint8_t face, int component, int textureComponent) -> uint64_t 
	{
		auto texture = waited.texture;

		ktx_uint8_t* ktxTextureData = ktxTexture_GetData(waited.ktxTexture);
		ktx_size_t ktxTextureSize = ktxTexture_GetDataSize(waited.ktxTexture);
		ktx_size_t ktxTextureMipmapOffset;
		auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max(size >> mip, 1ull); };
		const uint64_t mipedWidth = mipedSize(texture->handle->info->width, mipmap);
		const uint64_t mipedHeight = mipedSize(texture->handle->info->height, mipmap);
		KTX_error_code result = ktxTexture_GetImageOffset(waited.ktxTexture, mipmap, slice, face, &ktxTextureMipmapOffset);
		ktx_size_t ktxTextureMipmapSize = ktxTextureSize - ktxTextureMipmapOffset;
		ktx_size_t ktxTextureMipmapSize2 = mipedWidth * mipedHeight * component;
		if (component == textureComponent)
		{
			assert(ktxTextureMipmapOffset + ktxTextureMipmapSize2 <= ktxTextureSize);
			rendergraph_add_uploadtexturepass_ex(&rg, u8"upload texture", texture_handle, mipmap, face, ktxTextureMipmapSize2, 0, ktxTextureData + ktxTextureMipmapOffset, nullptr, 0, nullptr);
			return ktxTextureMipmapSize2;
		}
		else if (component == 3 && textureComponent == 4)
		{
			struct UploadTexturePassData
			{
				ktx_uint8_t* ktxTextureMipmapData;
				uint32_t width, height;
			};
			UploadTexturePassData* passdata;
			rendergraph_add_uploadtexturepass_ex(&rg, u8"upload texture", texture_handle, mipmap, 0, 0, 0, nullptr, [](HGEGraphics::UploadEncoder* encoder, void* passdata)
				{
					UploadTexturePassData* uploadpassdata = (UploadTexturePassData*)passdata;
					uint32_t component = 3;
					uint32_t textureComponent = 4;
					for (size_t i = 0; i < uploadpassdata->width * uploadpassdata->height; ++i)
					{
						upload(encoder, i * textureComponent, component, uploadpassdata->ktxTextureMipmapData + i * component);
					}
				}, sizeof(UploadTexturePassData), (void**)&passdata);
			auto size = mipedWidth * mipedHeight * textureComponent;

			passdata->ktxTextureMipmapData = ktxTextureData + ktxTextureMipmapOffset;
			passdata->width = mipedWidth;
			passdata->height = mipedHeight;

			return size;
		}
		else
			return 0;
	};

	uint64_t size = 0;
	uint32_t component = waited.component;
	uint32_t textureComponent = FormatUtil_BitSizeOfBlock(waited.texture->handle->info->format) / 8;

	for (uint32_t slice = 0; slice < waited.ktxTexture->numLayers; ++slice)
	{
		for (uint32_t face = 0; face < waited.ktxTexture->numFaces; ++face)
		{
			for (uint32_t mip = 0; mip < waited.ktxTexture->numLevels; ++mip)
			{
				size += uploadMip(rg, waited, texture_handle, mip, slice, face, component, textureComponent);
			}
		}
	}

	return size;
}

uint64_t uploadTexture(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, std::pmr::vector<HGEGraphics::texture_handle_t>& uploaded_texture_handles, WaitUploadTexture& waited)
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
			const uint64_t zBlocksCount = mipedSize(texture->handle->info->depth, 0);
			size = xBlocksCount * yBlocksCount * zBlocksCount * FormatUtil_BitSizeOfBlock(texture->handle->info->format) / 8;
			rendergraph_add_uploadtexturepass_ex(&rg, u8"upload texture", texture_handle, 0, 0, size, 0, waited.loader, [](HGEGraphics::UploadEncoder* encoder, void* passdata) {}, 0, nullptr);
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
	std::pmr::vector<HGEGraphics::texture_handle_t> uploaded_texture_handles(device->memory_resource);
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
		uploaded += uploadMesh(device, rg, uploaded_buffer_handle, waited);
	}

	auto passBuilder = rendergraph_add_holdpass(&rg, u8"upload texture holdon");
	for (auto& handle : uploaded_texture_handles)
		renderpass_sample(&passBuilder, handle);
	uploaded_texture_handles.clear();

	for (auto& handle : uploaded_buffer_handle)
		renderpass_use_buffer(&passBuilder, handle);
	uploaded_buffer_handle.clear();
}
