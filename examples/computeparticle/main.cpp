#include "framework.h"
#include "imgui.h"
#include <random>
#include <numeric>

struct ObjectData
{
	HMM_Mat4	wMatrix;
	HMM_Mat4	vpMatrix;
	HMM_Vec4	lightDir;
	HMM_Vec4	viewPos;
};

struct Application
{
	oval_device_t* device;
	CGPUSamplerId sampler = CGPU_NULLPTR;
	clock_t time;
	HGEGraphics::Mesh* quad;
	ObjectData object_data;
	float lightDirEulerX, lightDirEulerY;
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
	app.sampler = cgpu_create_sampler(app.device->device, &cubemap_sampler_desc);

	app.quad = oval_load_mesh(app.device, u8"media/models/Quad.obj");
}

void _free_resource(Application& app)
{
	free_mesh(app.quad);
	app.quad = nullptr;

	cgpu_free_sampler(app.sampler);
	app.sampler = nullptr;
}

void _init_world(Application& app)
{
	app.time = clock();
	app.lightDirEulerX = 180;
	app.lightDirEulerY = -30;
}

void on_update(oval_device_t* device)
{
	Application* app = (Application*)device->descriptor.userdata;

	auto now = clock();
	auto duration = (double)(now - app->time) / CLOCKS_PER_SEC;

	auto cameraParentMat = HMM_QToM4(HMM_QFromEuler_YXZ(HMM_AngleDeg(0), HMM_AngleDeg(24), 0));
	auto cameraLocalMat = HMM_Translate(HMM_V3(0, 0, -1));

	auto cameraMat = cameraParentMat * cameraLocalMat;

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


	auto objectMat = HMM_Translate(HMM_V3(0, 0, 0));

	auto lightRot = HMM_QFromEuler_YXZ(HMM_AngleDeg(app->lightDirEulerX), HMM_AngleDeg(app->lightDirEulerY), 0);
	auto lightDir = HMM_RotateV3Q(HMM_V3_Forward, lightRot);

	app->object_data.vpMatrix = vpMat;
	app->object_data.wMatrix = objectMat;
	app->object_data.lightDir = HMM_V4V(lightDir, 0);
	auto depth = app->object_data.viewPos.W;
	depth = duration * 0.15f;
	depth -= (int)depth;
	app->object_data.viewPos = HMM_V4V(eye, depth);
}

void on_imgui(oval_device_t* device)
{
	Application* app = (Application*)device->descriptor.userdata;

	if (ImGui::Button("Capture"))
		oval_render_debug_capture(device);
	ImGui::SliderFloat2("Light Dir", &app->lightDirEulerX, -180, 180);
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::resource_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	Application* app = (Application*)device->descriptor.userdata;

	auto object_ubo_handle = rendergraph_declare_uniform_buffer_quick(&rg, sizeof(ObjectData), &app->object_data);

	auto depth_handle = rendergraph_declare_texture(&rg);
	rg_texture_set_extent(&rg, depth_handle, rg_texture_get_width(&rg, rg_back_buffer), rg_texture_get_height(&rg, rg_back_buffer));
	rg_texture_set_depth_format(&rg, depth_handle, DepthBits::D24, true);

	auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
	uint32_t color = 0xff000000;
	renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
	renderpass_add_depth_attachment(&passBuilder, depth_handle, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD);
	renderpass_use_buffer(&passBuilder, object_ubo_handle);

	struct MainPassPassData
	{
		Application* app;
		buffer_handle_t object_ubo_handle;
	};
	MainPassPassData* passdata;
	renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* passdata)
		{
			MainPassPassData* resolved_passdata = (MainPassPassData*)passdata;
			Application& app = *resolved_passdata->app;

			//set_global_texture(encoder, app.noisemap, 0, 0);
			//set_global_sampler(encoder, app.sampler, 0, 1);
			//set_global_buffer(encoder, resolved_passdata->object_ubo_handle, 0, 2);
			//draw(encoder, app.texture3d, app.quad);
		}, sizeof(MainPassPassData), (void**)&passdata);
	passdata->app = app;
	passdata->object_ubo_handle = object_ubo_handle;
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