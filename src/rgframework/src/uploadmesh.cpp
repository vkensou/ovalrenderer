#include "uploadresource.h"

uint64_t uploadMesh(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, std::pmr::vector<HGEGraphics::buffer_handle_t>& uploaded_buffer_handle, WaitUploadMesh& waited)
{
	uint64_t uploaded = 0;
	auto mesh_vertex_handle = rendergraph_declare_buffer(&rg);
	rg_buffer_import(&rg, mesh_vertex_handle, waited.mesh->vertex_buffer);
	if (waited.vertex_data || waited.vertex_raw_data)
	{
		uint64_t size = waited.vertex_data_size;
		uploaded += size;
		rendergraph_add_uploadbufferpass_ex(&rg, u8"upload mesh vertex data", mesh_vertex_handle, size, 0, waited.vertex_data ? waited.vertex_data->data() : waited.vertex_raw_data, nullptr, 0, nullptr);
		uploaded_buffer_handle.push_back(mesh_vertex_handle);

		if (waited.vertex_data)
			device->delay_released_vertex_buffer.push_back(waited.vertex_data);
		if (waited.vertex_raw_data)
			device->delay_freeed_raw_data.push_back(waited.vertex_raw_data);
	}

	if (waited.index_data || waited.index_raw_data)
	{
		if (waited.mesh->index_buffer)
		{
			auto mesh_index_handle = rendergraph_declare_buffer(&rg);
			rg_buffer_import(&rg, mesh_index_handle, waited.mesh->index_buffer);
			uint64_t size = waited.index_data_size;
			uploaded += size;
			rendergraph_add_uploadbufferpass_ex(&rg, u8"upload mesh index data", mesh_index_handle, size, 0, waited.index_data ? waited.index_data->data() : waited.index_raw_data, nullptr, 0, nullptr);
			uploaded_buffer_handle.push_back(mesh_index_handle);
		}

		if (waited.index_data)
			device->delay_released_index_buffer.push_back(waited.index_data);
		if (waited.index_raw_data)
			device->delay_freeed_raw_data.push_back(waited.index_raw_data);
	}

	waited.mesh->prepared = true;
	return uploaded;
}
