#include "renderer.h"
#include "rendergraph.h"
#include "ktx.h"
#include <cassert>
#include "drawer.h"
#include "uploadresource.h"

typedef unsigned char stbi_uc;

void uploadResources(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg)
{
	if (device->wait_upload_mesh.empty())
		return;

	const uint32_t max_size = 1024 * 1024 * sizeof(uint32_t) * 10;
	uint32_t uploaded = 0;
	std::pmr::vector<HGEGraphics::buffer_handle_t> uploaded_buffer_handle(device->memory_resource);
	uploaded_buffer_handle.reserve(device->wait_upload_mesh.size() * 2);

	while (uploaded < max_size && !device->wait_upload_mesh.empty())
	{
		auto waited = device->wait_upload_mesh.front();
		device->wait_upload_mesh.pop();
		uploaded += uploadMesh(device, rg, uploaded_buffer_handle, waited);
	}

	auto passBuilder = rendergraph_add_holdpass(&rg, u8"upload texture holdon");

	for (auto& handle : uploaded_buffer_handle)
		renderpass_use_buffer(&passBuilder, handle);
	uploaded_buffer_handle.clear();
}
