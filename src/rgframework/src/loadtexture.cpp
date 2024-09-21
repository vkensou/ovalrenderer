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

HGEGraphics::Texture* load_texture_ktx(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	ktxResult result = KTX_SUCCESS;
	ktxTexture* ktxTexture;
	result = ktxTexture_CreateFromNamedFile((const char*)filepath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktxTexture);
	if (result != KTX_SUCCESS)
		return nullptr;

	auto [format, component] = detectKtxTextureFormat(ktxTexture);
	// TODO: support compressed ktxTexture
	if (ktxTexture->isCompressed || format == CGPU_FORMAT_UNDEFINED)
	{
		ktxTexture_Destroy(ktxTexture);
		return nullptr;
	}

	uint32_t width = ktxTexture->baseWidth;
	uint32_t height = ktxTexture->baseHeight;
	uint32_t mipLevels = ktxTexture->numLevels;
	uint32_t arraySize = 1;

	auto D = (oval_cgpu_device_t*)device;
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
		.owner_queue = D->gfx_queue,
		.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		.descriptors = descriptors,
	};

	auto texture = HGEGraphics::create_texture(D->device, texture_desc);

	D->wait_upload_texture.push({ texture, 1, nullptr, ktxTexture, generateMipmap, component });

	return texture;

	ktxTexture_Destroy(ktxTexture);

	return nullptr;
}

HGEGraphics::Texture* load_texture_raw(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	int width = 0, height = 0, components = 0;
	auto texture_loader = stbi_load((const char*)filepath, &width, &height, &components, 4);
	if (!texture_loader)
		return nullptr;

	const char* filename = nullptr;
	if (filepath)
	{
		filename = strrchr((const char*)filepath, '/');
		if (!filename)
			filename = strrchr((const char*)filepath, '\\');
		filename = filename ? filename + 1 : (const char*)filepath;
	}

	auto D = (oval_cgpu_device_t*)device;
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
		.owner_queue = D->gfx_queue,
		.start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		.descriptors = CGPUResourceTypes(mipmap ? CGPU_RESOURCE_TYPE_TEXTURE | CGPU_RESOURCE_TYPE_RENDER_TARGET : CGPU_RESOURCE_TYPE_TEXTURE),
	};

	auto texture = HGEGraphics::create_texture(D->device, texture_desc);

	D->wait_upload_texture.push({ texture, 0, texture_loader, nullptr, mipmap && texture->handle->info->mip_levels > 1, 4 });

	return texture;
}

HGEGraphics::Texture* oval_load_texture(oval_device_t* device, const char8_t* filepath, bool mipmap)
{
	if (endsWithKtx((const char*)filepath))
		return load_texture_ktx(device, filepath, mipmap);
	else
		return load_texture_raw(device, filepath, mipmap);
}
