#include "framework.h"
#include "imgui.h"
#include <time.h>

struct SkyboxData
{
	HMM_Mat4 vpMatrixI;
	HMM_Vec4 param;
};

struct UnlitObjectData
{
	HMM_Mat4 wMatrix;
	HMM_Mat4 vpMatrix;
};

struct HDRObjectData
{
	HMM_Mat4 wMatrix;
	HMM_Mat4 vpMatrix;
	HMM_Vec4 lightDir;
	HMM_Vec4 viewPos;
	HMM_Vec4 albedo;
};

struct Application
{
	oval_device_t* device;
	HGEGraphics::Texture* cubemap;
	HGEGraphics::Texture* colormap;
	CGPUSamplerId cubemap_sampler = CGPU_NULLPTR;
	HGEGraphics::Shader* skybox_shader;
	HGEGraphics::Shader* unlit_shader;
	HGEGraphics::Shader* hdr_shader;
	SkyboxData skybox_data;
	UnlitObjectData object_data;
	HDRObjectData hdr_data;
	clock_t time;
	HGEGraphics::Mesh* quad;
	HGEGraphics::Mesh* sphere;
	float lightDirEulerX, lightDirEulerY;
	float cameraRotateY;
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
	app.skybox_shader = oval_create_shader(app.device, "hdr/skybox.vert.spv", "hdr/skybox.frag.spv", blend_desc, depth_desc, rasterizer_state);
	app.unlit_shader = oval_create_shader(app.device, "hdr/unlit.vert.spv", "hdr/unlit.frag.spv", blend_desc, depth_desc, rasterizer_state);
	app.hdr_shader = oval_create_shader(app.device, "hdr/hdr.vert.spv", "hdr/hdr.frag.spv", blend_desc, depth_desc, rasterizer_state);

	app.cubemap = oval_load_texture(app.device, u8"media/textures/uffizi_cube.ktx", true);
	app.colormap = oval_load_texture(app.device, u8"media/textures/TilesGray512.ktx", true);

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
	app.cubemap_sampler = oval_create_sampler(app.device, &cubemap_sampler_desc);

	app.quad = oval_load_mesh(app.device, u8"media/models/Quad.obj");
	app.sphere = oval_load_mesh(app.device, u8"media/models/Sphere.obj");
}

void _free_resource(Application& app)
{
	oval_free_mesh(app.device, app.quad);
	app.quad = nullptr;

	oval_free_mesh(app.device, app.sphere);
	app.sphere = nullptr;

	oval_free_texture(app.device, app.cubemap);
	app.cubemap = nullptr;

	oval_free_sampler(app.device, app.cubemap_sampler);
	app.cubemap_sampler = nullptr;

	oval_free_texture(app.device, app.colormap);
	app.colormap = nullptr;

	oval_free_shader(app.device, app.skybox_shader);
	app.skybox_shader = nullptr;

	oval_free_shader(app.device, app.unlit_shader);
	app.unlit_shader = nullptr;

	oval_free_shader(app.device, app.hdr_shader);
	app.hdr_shader = nullptr;
}

void _init_world(Application& app)
{
	app.time = clock();
	app.hdr_data.albedo = HMM_V4(1, 1, 1, 0.5);
	app.lightDirEulerX = -50;
	app.lightDirEulerY = 150;
	app.cameraRotateY = 0;
}

void on_update(oval_device_t* device)
{
	Application* app = (Application*)device->descriptor.userdata;

	auto now = clock();
	auto duration = (double)(now - app->time) / CLOCKS_PER_SEC;

	auto cameraParentMat = HMM_QToM4(HMM_QFromEuler_YXZ(HMM_AngleDeg(0), HMM_AngleDeg(app->cameraRotateY), 0));
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

	app->skybox_data.vpMatrixI = vpMatI;
	app->skybox_data.param = HMM_V4(eye.X, eye.Y, eye.Z, far);

	auto objectMat = HMM_Translate(HMM_V3(0, 0, 0));

	app->object_data.wMatrix = objectMat;
	app->object_data.vpMatrix = vpMat;

	auto lightRot = HMM_QFromEuler_YXZ(HMM_AngleDeg(app->lightDirEulerX), HMM_AngleDeg(app->lightDirEulerY), 0);
	auto lightDir = HMM_RotateV3Q(HMM_V3_Forward, lightRot);

	app->hdr_data.wMatrix = objectMat;
	app->hdr_data.vpMatrix = vpMat;
	app->hdr_data.lightDir = HMM_V4V(lightDir, 0);
	app->hdr_data.viewPos = HMM_V4V(eye, 0);
}

void on_imgui(oval_device_t* device)
{
	Application* app = (Application*)device->descriptor.userdata;

	if (ImGui::Button("Capture"))
		oval_render_debug_capture(device);
	ImGui::SliderFloat("Camera Dir", &app->cameraRotateY, -360, 360);
	ImGui::SliderFloat("Smoothness", &app->hdr_data.albedo.W, 0, 1);
	ImGui::SliderFloat2("Light Dir", &app->lightDirEulerX, -360, 360);
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::texture_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	Application* app = (Application*)device->descriptor.userdata;

	auto skybox_ubo_handle = rendergraph_declare_uniform_buffer_quick(&rg, sizeof(SkyboxData), &app->skybox_data);
	auto object_ubo_handle = rendergraph_declare_uniform_buffer_quick(&rg, sizeof(UnlitObjectData), &app->object_data);
	auto hdr_ubo_handle = rendergraph_declare_uniform_buffer_quick(&rg, sizeof(HDRObjectData), &app->hdr_data);

	auto depth_handle = rendergraph_declare_texture(&rg);
	rg_texture_set_extent(&rg, depth_handle, rg_texture_get_width(&rg, rg_back_buffer), rg_texture_get_height(&rg, rg_back_buffer));
	rg_texture_set_depth_format(&rg, depth_handle, DepthBits::D24, true);

	auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
	uint32_t color = 0xff000000;
	renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
	renderpass_add_depth_attachment(&passBuilder, depth_handle, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD);
	renderpass_use_buffer(&passBuilder, skybox_ubo_handle);
	renderpass_use_buffer(&passBuilder, object_ubo_handle);
	renderpass_use_buffer(&passBuilder, hdr_ubo_handle);

	struct MainPassPassData
	{
		Application* app;
		HGEGraphics::buffer_handle_t skybox_ubo_handle;
		HGEGraphics::buffer_handle_t object_ubo_handle;
		HGEGraphics::buffer_handle_t hdr_ubo_handle;
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

			set_global_texture(encoder, app.colormap, 0, 0);
			set_global_sampler(encoder, app.cubemap_sampler, 0, 1);
			set_global_buffer(encoder, resolved_passdata->object_ubo_handle, 0, 2);
			//draw(encoder, app.unlit_shader, app.quad);

			set_global_buffer(encoder, resolved_passdata->hdr_ubo_handle, 0, 0);
			set_global_texture(encoder, app.colormap, 0, 1);
			set_global_sampler(encoder, app.cubemap_sampler, 0, 2);
			set_global_texture(encoder, app.cubemap, 0, 3);
			draw(encoder, app.hdr_shader, app.sphere);
		}, sizeof(MainPassPassData), (void**)&passdata);
	passdata->app = app;
	passdata->skybox_ubo_handle = skybox_ubo_handle;
	passdata->object_ubo_handle = object_ubo_handle;
	passdata->hdr_ubo_handle = hdr_ubo_handle;
}

extern "C"
int SDL_main(int argc, char *argv[])
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