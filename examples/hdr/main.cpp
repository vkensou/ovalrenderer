#include "framework.h"
#include "imgui.h"
#include <time.h>

struct SkyboxData
{
	HMM_Mat4 vpMatrixI;
	HMM_Vec4 param;
};

struct Application
{
	oval_device_t* device;
	HGEGraphics::Texture* cubemap;
	CGPUSamplerId cubemap_sampler = CGPU_NULLPTR;
	HGEGraphics::Shader* skybox_shader;
	SkyboxData skybox_data;
	clock_t time;
};

void _init_resource(Application& app)
{
	CGPUBlendStateDescriptor blend_desc = {
		.src_factors = { CGPU_BLEND_CONST_ONE },
		.dst_factors = { CGPU_BLEND_CONST_ZERO },
		.src_alpha_factors = { CGPU_BLEND_CONST_ONE },
		.dst_alpha_factors = { CGPU_BLEND_CONST_ZERO },
		.blend_modes = { CGPU_BLEND_MODE_ADD },
		.blend_alpha_modes = { CGPU_BLEND_MODE_ADD },
		.masks = { CGPU_COLOR_MASK_ALL },
		.alpha_to_coverage = false,
		.independent_blend = false,
	};
	CGPUDepthStateDesc depth_desc = {
		.depth_test = true,
		.depth_write = true,
		.depth_func = CGPU_CMP_GEQUAL,
		.stencil_test = false,
	};
	CGPURasterizerStateDescriptor rasterizer_state = {
		.cull_mode = CGPU_CULL_MODE_BACK,
	};
	app.skybox_shader = HGEGraphics::create_shader(app.device->device, "hdr/skybox.vert.spv", "hdr/skybox.frag.spv", blend_desc, depth_desc, rasterizer_state);

	app.cubemap = oval_load_texture(app.device, u8"media/textures/uffizi_cube.ktx", true);

	CGPUSamplerDescriptor cubemap_sampler_desc = {
		.min_filter = CGPU_FILTER_TYPE_LINEAR,
		.mag_filter = CGPU_FILTER_TYPE_LINEAR,
		.mipmap_mode = CGPU_MIPMAP_MODE_LINEAR,
		.address_u = CGPU_ADDRESS_MODE_REPEAT,
		.address_v = CGPU_ADDRESS_MODE_REPEAT,
		.address_w = CGPU_ADDRESS_MODE_REPEAT,
		.mip_lod_bias = 0,
		.max_anisotropy = 1,
	};
	app.cubemap_sampler = cgpu_create_sampler(app.device->device, &cubemap_sampler_desc);
}

void _free_resource(Application& app)
{
	free_texture(app.cubemap);
	app.cubemap = nullptr;

	cgpu_free_sampler(app.cubemap_sampler);
	app.cubemap_sampler = nullptr;

	free_shader(app.skybox_shader);
	app.skybox_shader = nullptr;
}

void _init_world(Application& app)
{
	app.time = clock();
}

void on_update(oval_device_t* device)
{
	Application* app = (Application*)device->descriptor.userdata;

	auto now = clock();
	auto duration = (double)(now - app->time) / CLOCKS_PER_SEC;

	auto cameraMat = HMM_Translate(HMM_V3(100, 0, 0)) * HMM_M4FromEuler_YXZ(0, 180 * HMM_DegToRad, 0);

	auto eye = HMM_M4GetTranslate(cameraMat);
	auto forward = HMM_M4GetForward(cameraMat);
	auto viewMat = HMM_LookAt2_LH(eye, forward, HMM_V3_Up);

	float aspect = (float)app->device->descriptor.width / app->device->descriptor.height;
	float near = 0.1f;
	float far = 256;
	float fov = 60;

	auto projMat = HMM_Perspective_LH_RO(fov * HMM_DegToRad, aspect, near, far);
	auto vpMat = projMat * viewMat;
	auto vpMatI = HMM_InvGeneral(vpMat);
	auto pI = HMM_InvGeneral(projMat);
	auto p1 = HMM_V4(-1 * far, -1 * far, 0, far);
	auto q1 = HMM_Mul(pI, p1);

	float t2 = tanf(fov / 2);

	app->skybox_data.vpMatrixI = vpMatI;
	app->skybox_data.param = HMM_V4(eye.X, eye.Y, eye.Z, far);
}

void on_imgui(oval_device_t* device)
{
	if (ImGui::Button("Capture"))
		oval_render_debug_capture(device);
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::resource_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	Application* app = (Application*)device->descriptor.userdata;

	auto skybox_ubo_handle = rendergraph_declare_buffer(&rg);
	rg_buffer_set_size(&rg, skybox_ubo_handle, sizeof(SkyboxData));
	rg_buffer_set_type(&rg, skybox_ubo_handle, CGPU_RESOURCE_TYPE_UNIFORM_BUFFER);
	rg_buffer_set_usage(&rg, skybox_ubo_handle, ECGPUMemoryUsage::CGPU_MEM_USAGE_GPU_ONLY);
	rendergraph_add_uploadbufferpass_ex(&rg, u8"upload ubo", skybox_ubo_handle, sizeof(SkyboxData), 0, &app->skybox_data, nullptr, 0, nullptr);

	auto depth_handle = rendergraph_declare_texture(&rg);
	rg_texture_set_extent(&rg, depth_handle, rg_texture_get_width(&rg, rg_back_buffer), rg_texture_get_height(&rg, rg_back_buffer));
	rg_texture_set_depth_format(&rg, depth_handle, DepthBits::D24, true);

	auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
	uint32_t color = 0xff000000;
	renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
	renderpass_add_depth_attachment(&passBuilder, depth_handle, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD);
	renderpass_use_buffer(&passBuilder, skybox_ubo_handle);

	struct MainPassPassData
	{
		Application* app;
		HGEGraphics::buffer_handle_t skybox_ubo_handle;
	};
	MainPassPassData* passdata;
	renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* passdata)
		{
			MainPassPassData* resolved_passdata = (MainPassPassData*)passdata;
			Application& app = *resolved_passdata->app;
			set_global_texture(encoder, app.cubemap, 0, 0);
			set_global_sampler(encoder, app.cubemap_sampler, 0, 1);
			set_global_buffer(encoder, resolved_passdata->skybox_ubo_handle, 0, 2);
			draw_procedure(encoder, app.skybox_shader, CGPU_PRIM_TOPO_TRI_LIST, 3);
		}, sizeof(MainPassPassData), (void**)&passdata);
	passdata->app = app;
	passdata->skybox_ubo_handle = skybox_ubo_handle;
}

int main()
{
	const int width = 800;
	const int height = 600;
	Application app;
	oval_device_descriptor device_descriptor =
	{
		.userdata = &app,
		.on_update = on_update,
		.on_imgui = on_imgui,
		.on_draw = on_draw,
		.width = width,
		.height = height,
		.enable_capture = false,
		.enable_profile = false,
	};
	app.device = oval_create_device(&device_descriptor);
	_init_resource(app);
	_init_world(app);
	if (app.device)
	{
		oval_runloop(app.device);
		_free_resource(app);
		oval_free_device(app.device);
	}

	return 0;
}