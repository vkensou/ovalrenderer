#pragma once

#include "cgpu_device.h"
#include "renderer.h"

uint64_t uploadMesh(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg, std::pmr::vector<HGEGraphics::buffer_handle_t>& uploaded_buffer_handle, WaitUploadMesh& waited);
void uploadResources(oval_cgpu_device_t* device, HGEGraphics::rendergraph_t& rg);
