#include "renderer.h"
#include "rendergraph.h"
#include "ktx.h"
#include <cassert>

typedef unsigned char stbi_uc;

struct WaitUploadTexture
{
	HGEGraphics::Texture* texture;
	int loader_type;
	stbi_uc* loader;
	ktxTexture* ktxTexture;
	bool generate_mipmap;
	int component;
};

uint64_t uploadKTXTexture(HGEGraphics::rendergraph_t& rg, WaitUploadTexture& waited, HGEGraphics::resource_handle_t texture_handle)
{
	auto uploadMip = [](HGEGraphics::rendergraph_t& rg, WaitUploadTexture& waited, HGEGraphics::resource_handle_t texture_handle, uint8_t mipmap, uint8_t slice, uint8_t face, int component, int textureComponent) -> uint64_t 
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
