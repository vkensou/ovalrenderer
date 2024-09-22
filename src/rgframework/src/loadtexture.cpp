#include "cgpu_device.h"

static bool endsWithKtx(const char* str) {
	const char* suffix = ".ktx";
	size_t len = strlen(str);
	size_t suffixLen = strlen(suffix);
	if (len >= suffixLen) {
		return strncmp(str + len - suffixLen, suffix, suffixLen) == 0;
	}
	return false;
}

std::pair<ECGPUFormat, int> detectKtxTextureFormat(ktxTexture* ktxTexture)
{
	if (ktxTexture->classId == ktxTexture1_c)
	{
		auto ktx1 = (ktxTexture1*)ktxTexture;
		switch (ktx1->glInternalformat)
		{
		case 0x1908:
		case 0x8058:
			return { CGPU_FORMAT_R8G8B8A8_SRGB, 4 };
		case 0x881A:
			return { CGPU_FORMAT_R16G16B16A16_SFLOAT, 8 };
		}
		printf("format: %d\n", ktx1->glFormat);
	}
	else if (ktxTexture->classId == ktxTexture2_c)
	{
		auto ktx2 = (ktxTexture2*)ktxTexture;
		switch (ktx2->vkFormat)
		{
		case 23:
			return { CGPU_FORMAT_R8G8B8A8_SRGB, 3 };
		}
		printf("format: %d\n", ktx2->vkFormat);
	}
	return { CGPU_FORMAT_UNDEFINED, 0 };
}

uint64_t load_texture_ktx(oval_cgpu_device_t* device, oval_graphics_transfer_queue_t queue, HGEGraphics::Texture* texture, const char8_t* filepath, bool mipmap)
{
	ktxResult result = KTX_SUCCESS;
	ktxTexture* ktxTexture;
	result = ktxTexture_CreateFromNamedFile((const char*)filepath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
	if (result != KTX_SUCCESS)
		return 0;

	auto [format, component] = detectKtxTextureFormat(ktxTexture);
	// TODO: support compressed ktxTexture
	if (ktxTexture->isCompressed || format == CGPU_FORMAT_UNDEFINED)
	{
		ktxTexture_Destroy(ktxTexture);
		return 0;
	}

	uint32_t width = ktxTexture->baseWidth;
	uint32_t height = ktxTexture->baseHeight;
	uint32_t mipLevels = ktxTexture->numLevels;
	uint32_t arraySize = 1;

	bool generateMipmap = mipmap && mipLevels <= 1;
	mipLevels = mipmap ? (mipLevels > 1 ? mipLevels : (static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1)) : 1;
	CGPUResourceTypes descriptors = CGPU_RESOURCE_TYPE_TEXTURE;
	if (generateMipmap)
		descriptors |= CGPU_RESOURCE_TYPE_RENDER_TARGET;
	if (ktxTexture->isCubemap)
	{
		descriptors |= CGPU_RESOURCE_TYPE_TEXTURE_CUBE;
		arraySize = 6;
	}
	CGPUTextureDescriptor texture_desc =
	{
		.name = filepath,
		.width = (uint64_t)width,
		.height = (uint64_t)height,
		.depth = 1,
		.array_size = arraySize,
		.format = format,
		.mip_levels = mipLevels,
		.owner_queue = device->gfx_queue,
		.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		.descriptors = descriptors,
	};

	HGEGraphics::init_texture(texture, device->device, texture_desc);

	uint64_t size = width * height * component;
	auto mipedSize = [](uint64_t size, uint64_t mip) { return std::max(size >> mip, 1ull); };
	for (size_t mipmap = 0; mipmap < mipLevels; ++mipmap)
	{
		const uint64_t mipedWidth = mipedSize(width, mipmap);
		const uint64_t mipedHeight = mipedSize(height, mipmap);
		size += mipedWidth * mipedHeight * component * arraySize;
	}

	uint32_t textureComponent = FormatUtil_BitSizeOfBlock(texture->handle->info->format) / 8;
	auto data = oval_graphics_transfer_queue_transfer_data_to_texture(queue, size, texture, generateMipmap);
	{
		auto offset_data = data;
		auto ktxTextureData = ktxTexture_GetData(ktxTexture);
		auto ktxTextureDataSize = ktxTexture_GetDataSize(ktxTexture);
		for (uint32_t mip = 0; mip < ktxTexture->numLevels; ++mip)
		{
			const uint64_t mipedWidth = mipedSize(width, mip);
			const uint64_t mipedHeight = mipedSize(height, mip);
			for (size_t slice = 0; slice < ktxTexture->numLayers; ++slice)
			{
				for (size_t face = 0; face < ktxTexture->numFaces; ++face)
				{
					ktx_size_t ktxTextureMipmapSize = mipedWidth * mipedHeight * component;
					ktx_size_t ktxTextureMipmapOffset;
					KTX_error_code result = ktxTexture_GetImageOffset(ktxTexture, mip, slice, face, &ktxTextureMipmapOffset);
					if (textureComponent == component)
					{
						memcpy(data, ktxTextureData + ktxTextureMipmapOffset, ktxTextureMipmapSize);
					}
					else
					{
						for (size_t i = 0; i < mipedWidth * mipedHeight; ++i)
						{
							assert(ktxTextureDataSize >= ktxTextureMipmapOffset + i * component + component);
							assert(data + size >= offset_data + i * textureComponent + textureComponent);
							memcpy(offset_data + i * textureComponent, ktxTextureData + ktxTextureMipmapOffset + i * component, component);
						}
					}
					offset_data += mipedWidth * mipedHeight * textureComponent;
				}
			}
		}
	}

	ktxTexture_Destroy(ktxTexture);

	return size;
}

uint64_t load_texture_raw(oval_cgpu_device_t* device, oval_graphics_transfer_queue_t queue, HGEGraphics::Texture* texture, const char8_t* filepath, bool mipmap)
{
	int width = 0, height = 0, components = 0;
	auto texture_loader = stbi_load((const char*)filepath, &width, &height, &components, 4);
	if (!texture_loader)
	{
		assert(texture_loader && "load texture filed");
		return 0;
	}

	const char* filename = nullptr;
	if (filepath)
	{
		filename = strrchr((const char*)filepath, '/');
		if (!filename)
			filename = strrchr((const char*)filepath, '\\');
		filename = filename ? filename + 1 : (const char*)filepath;
	}

	auto mipLevels = mipmap ? static_cast<uint32_t>(std::floor(std::log2(std::max(width, height)))) + 1 : 1;
	CGPUTextureDescriptor texture_desc =
	{
		.name = (const char8_t*)filename,
		.width = (uint64_t)width,
		.height = (uint64_t)height,
		.depth = 1,
		.array_size = 1,
		.format = CGPU_FORMAT_R8G8B8A8_SRGB,
		.mip_levels = mipLevels,
		.owner_queue = device->gfx_queue,
		.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		.descriptors = CGPUResourceTypes(mipmap ? CGPU_RESOURCE_TYPE_TEXTURE | CGPU_RESOURCE_TYPE_RENDER_TARGET : CGPU_RESOURCE_TYPE_TEXTURE),
	};

	HGEGraphics::init_texture(texture, device->device, texture_desc);

	uint64_t size = width * height * 4;
	bool genenrate_mipmap = mipmap && texture->handle->info->mip_levels > 1;
	auto data = oval_graphics_transfer_queue_transfer_data_to_texture(queue, size, texture, genenrate_mipmap);
	memcpy(data, texture_loader, size);

	return size;
}

HGEGraphics::Texture* oval_load_texture(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	auto D = (oval_cgpu_device_t*)device;

	WaitLoadResource resource;
	resource.type = Texture;
	size_t path_size = strlen((const char*)filepath) + 1;
	char8_t* path = (char8_t*)D->memory_resource->allocate(path_size);
	memcpy(path, filepath, path_size);
	resource.textureResource = {
		.texture = HGEGraphics::create_empty_texture(),
		.path = path,
		.mipmap = mipmap,
	};
	resource.textureResource.texture->prepared = false;
	D->wait_load_resources.push(resource);
	return resource.textureResource.texture;
}

void oval_load_texture_queue(oval_cgpu_device_t* device)
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
		if (waited.type == Texture)
		{
			auto& textureResource = waited.textureResource;
			if (endsWithKtx((const char*)textureResource.path))
				uploaded += load_texture_ktx(device, queue, textureResource.texture, textureResource.path, textureResource.mipmap);
			else
				uploaded += load_texture_raw(device, queue, textureResource.texture, textureResource.path, textureResource.mipmap);
			waited.textureResource.texture->prepared = true;
		}
	}

	oval_graphics_transfer_queue_submit(&device->super, queue);
}