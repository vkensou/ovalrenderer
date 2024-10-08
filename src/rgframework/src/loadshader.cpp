#include "cgpu_device.h"
#include <fstream>

#ifdef _WIN32
static std::vector<char> readFile(const std::string& filename)
{
	std::ifstream file("assets/" + filename, std::ios::ate | std::ios::binary);

	if (!file.is_open())
	{
		throw std::runtime_error("failed to open file!");
	}

	size_t fileSize = (size_t)file.tellg();
	std::vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);

	file.close();
	return buffer;
}
#endif

HGEGraphics::Shader* oval_create_shader(oval_device_t* device, const std::string& vertPath, const std::string& fragPath, const CGPUBlendStateDescriptor& blend_desc, const CGPUDepthStateDesc& depth_desc, const CGPURasterizerStateDescriptor& rasterizer_state)
{
	auto D = (oval_cgpu_device_t*)device;
	auto vertShaderCode = readFile(vertPath);
	auto fragShaderCode = readFile(fragPath);
	return HGEGraphics::create_shader(D->device, reinterpret_cast<const uint8_t*>(vertShaderCode.data()), (uint32_t)vertShaderCode.size(), reinterpret_cast<const uint8_t*>(fragShaderCode.data()), (uint32_t)fragShaderCode.size(),
		blend_desc, depth_desc, rasterizer_state);
}

void oval_free_shader(oval_device_t* device, HGEGraphics::Shader* shader)
{
	HGEGraphics::free_shader(shader);
}

HGEGraphics::ComputeShader* oval_create_compute_shader(oval_device_t* device, const std::string& compPath)
{
	auto D = (oval_cgpu_device_t*)device;
	auto compShaderCode = readFile(compPath);
	return HGEGraphics::create_compute_shader(D->device, reinterpret_cast<const uint8_t*>(compShaderCode.data()), compShaderCode.size());
}

void oval_free_compute_shader(oval_device_t* device, HGEGraphics::ComputeShader* shader)
{
	HGEGraphics::free_compute_shader(shader);
}
