#include "framework.h"
#include "imgui.h"

struct ObjectData
{
	HMM_Mat4	wMatrix;
	HMM_Mat4	vpMatrix;
	HMM_Vec4	lightDir;
	HMM_Vec4	shininess;
	HMM_Vec4	viewPos;
	HMM_Vec4	albedo;
};

struct BallData
{
	HMM_Vec3	position;
	HMM_Vec3	scale;
	uint32_t		color;
	uint64_t		frame;
	float			frameInterpolator;
};

const std::array<HMM_Vec2, 15> gridPosFrames
{ {
	HMM_Vec2{  0.0f - 0.75, -3.0f },
	HMM_Vec2{ -1.0f - 0.75, -3.0f },
	HMM_Vec2{ -2.0f - 0.75, -3.0f },
	HMM_Vec2{ -2.0f - 0.75, -2.0f },
	HMM_Vec2{ -2.0f - 0.75, -1.0f },
	HMM_Vec2{ -2.0f - 0.75,  0.0f },
	HMM_Vec2{ -2.0f - 0.75, +1.0f },
	HMM_Vec2{ -2.0f - 0.75, +2.0f },
	HMM_Vec2{ -1.0f - 0.75, +2.0f },
	HMM_Vec2{  0.0f - 0.75, +2.0f },
	HMM_Vec2{  1.0f - 0.75, +2.0f },
	HMM_Vec2{  2.0f - 0.75, +2.0f },
	HMM_Vec2{  3.0f - 0.75, +2.0f },
	HMM_Vec2{  3.0f - 0.75, +1.0f },
	HMM_Vec2{  3.0f - 0.75,  0.0f }
} };

const float ballJumpHeight = 0.5f;

struct Application
{
	oval_device_t* device;
	HGEGraphics::Shader* shader;
	CGPUSamplerId texture_sampler = CGPU_NULLPTR;
	HGEGraphics::Texture* color_map;
	HGEGraphics::Mesh* mesh1;
	HGEGraphics::Mesh* mesh2;
	HGEGraphics::Mesh* mesh3;
	std::array<ObjectData, 5> objects;
	std::array<BallData, 3> balls;
	std::array<HGEGraphics::Mesh*, 5> meshs;
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
	app.shader = HGEGraphics::create_shader(app.device->device, "animation/object.vert.spv", "animation/object.frag.spv", blend_desc, depth_desc, rasterizer_state);

	CGPUSamplerDescriptor texture_sampler_desc = {
		.min_filter = CGPU_FILTER_TYPE_LINEAR,
		.mag_filter = CGPU_FILTER_TYPE_LINEAR,
		.mipmap_mode = CGPU_MIPMAP_MODE_LINEAR,
		.address_u = CGPU_ADDRESS_MODE_REPEAT,
		.address_v = CGPU_ADDRESS_MODE_REPEAT,
		.address_w = CGPU_ADDRESS_MODE_REPEAT,
		.mip_lod_bias = 0,
		.max_anisotropy = 1,
	};
	app.texture_sampler = cgpu_create_sampler(app.device->device, &texture_sampler_desc);

	app.color_map = oval_load_texture(app.device, u8"media/textures/TilesGray512.ktx", true);

	app.mesh1 = oval_load_mesh(app.device, u8"media/models/PenroseStairs-Top.obj");
	app.mesh2 = oval_load_mesh(app.device, u8"media/models/PenroseStairs-Bottom.obj");
	app.mesh3 = oval_load_mesh(app.device, u8"media/models/IcoSphere.obj");
}

void _free_resource(Application& app)
{
	oval_free_texture(app.device, app.color_map);
	app.color_map = nullptr;

	oval_free_mesh(app.device, app.mesh1);
	app.mesh1 = nullptr;

	oval_free_mesh(app.device, app.mesh2);
	app.mesh2 = nullptr;

	oval_free_mesh(app.device, app.mesh3);
	app.mesh3 = nullptr;

	free_shader(app.shader);
	app.shader = nullptr;

	cgpu_free_sampler(app.texture_sampler);
	app.texture_sampler = nullptr;
}

HMM_Vec3 GetGridPos(std::size_t frame)
{
	return HMM_Vec3
	{
		gridPosFrames[frame].X + 0.5f,
		3.3f - static_cast<float>(frame) * 0.2f,
		gridPosFrames[frame].Y + 0.5f
	};
}

void _init_world(Application& app)
{
	memset(app.objects.data(), 0, app.objects.size() * sizeof(ObjectData));
	app.objects[3].wMatrix = HMM_M4_Identity;
	app.objects[4].wMatrix = HMM_M4_Identity;
	app.balls[0] = { .position = GetGridPos(0), .scale = HMM_V3_One, .color = 0xffff0000, .frame = 0, .frameInterpolator = 0 };
	app.balls[1] = { .position = GetGridPos(5), .scale = HMM_V3_One, .color = 0xff00ff00, .frame = 5, .frameInterpolator = 0.33 };
	app.balls[2] = { .position = GetGridPos(10), .scale = HMM_V3_One, .color = 0xff0000ff, .frame = 10, .frameInterpolator = 0.66 };
	app.objects[0].albedo = HMM_V4(1, 0, 0, 0);
	app.objects[1].albedo = HMM_V4(0, 1, 0, 0);
	app.objects[2].albedo = HMM_V4(0, 0, 1, 0);
	app.objects[3].albedo = HMM_V4(1, 1, 1, 1);
	app.objects[4].albedo = HMM_V4(1, 1, 0, 0);

	auto cameraParentMat = HMM_QToM4(HMM_QFromEuler_YXZ(HMM_AngleDeg(33.4), HMM_AngleDeg(45), 0));
	auto cameraLocalMat = HMM_Translate(HMM_V3(0, 0, -15));

	auto cameraMat = cameraParentMat * cameraLocalMat;
	auto eye = HMM_M4GetTranslate(cameraMat);
	auto forward = HMM_M4GetForward(cameraMat);
	auto viewMat = HMM_LookAt2_LH(eye, forward, HMM_V3_Up);

	float aspect = 800.f / 600.f;
	float winSize = 6;
	float near = 0.1f;
	float far = 100;

	auto proj = HMM_Orthographic2_LH_RO(winSize, aspect, near, far);
	auto vpMat = proj * viewMat;

	for (auto& objectData : app.objects)
	{
		auto lightDir = HMM_Norm(HMM_V3(0.25f, -0.7f, 1.25f));

		objectData.lightDir = HMM_V4V(lightDir, 0);
		objectData.shininess = HMM_V4(90, 0, 0, 0);

		objectData.viewPos = HMM_V4V(eye, 0);

		objectData.vpMatrix = vpMat;
	}

	app.meshs[0] = app.mesh3;
	app.meshs[1] = app.mesh3;
	app.meshs[2] = app.mesh3;
	app.meshs[3] = app.mesh1;
	app.meshs[4] = app.mesh2;
}

void on_update(oval_device_t* device)
{
	Application* app = (Application*)device->descriptor.userdata;

	float dt = device->deltaTime;
	for (auto& ball : app->balls)
	{
		ball.frameInterpolator += dt * 2;
		while (ball.frameInterpolator > 1.0f)
		{
			ball.frameInterpolator -= 1.0f;
			ball.frame++;

			if (ball.frame + 1 >= gridPosFrames.size())
			{
				ball.position = GetGridPos(0);
				ball.frame = 0;
			}
		}

		float t = ball.frameInterpolator;
		float ts = (t);

		// Add scaling to animate bouncing
		float s = 0.0f;
		if (ts <= 0.1f)
			s = std::cos((ts * 5.0f) * HMM_PI);
		else if (ts >= 0.9f)
			s = std::sin(((ts - 0.9f) * 5.0f) * HMM_PI);

		// Interpolate transformation between current and next frame
		ball.position = HMM_Lerp(GetGridPos(ball.frame), t, GetGridPos(ball.frame + 1));
		ball.position.Y += std::sin(t * HMM_PI) * ballJumpHeight;
		ball.scale = HMM_V3(1.0f + s * 0.1f, 1.0f - s * 0.3f, 1.0f + s * 0.1f);
	}

	for (size_t i = 0; i < app->balls.size(); ++i)
	{
		auto& ball = app->balls[i];
		auto& data = app->objects[i];

		data.wMatrix = HMM_TRS(ball.position, HMM_Q_Identity, ball.scale * 0.3);
	}
}

void on_imgui(oval_device_t* device)
{
	ImGui::Text("Hello, ImGui!");
	if (ImGui::Button("Capture"))
		oval_render_debug_capture(device);

	uint32_t length;
	const char8_t** names;
	const float* durations;
	oval_query_render_profile(device, &length, &names, &durations);
	if (length > 0)
	{
		float total_duration = 0.f;
		for (uint32_t i = 0; i < length; ++i)
		{
			float duration = durations[i] * 1000;
			ImGui::Text("%s %7.2f us", names[i], duration);
			total_duration += duration;
		}
		ImGui::Text("Total Time: %7.2f us", total_duration);
	}
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::texture_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	Application* app = (Application*)device->descriptor.userdata;

	auto ubo_handle = rendergraph_declare_uniform_buffer_quick(&rg, app->objects.size() * sizeof(ObjectData), app->objects.data());

	auto depth_handle = rendergraph_declare_texture(&rg);
	rg_texture_set_extent(&rg, depth_handle, rg_texture_get_width(&rg, rg_back_buffer), rg_texture_get_height(&rg, rg_back_buffer));
	rg_texture_set_depth_format(&rg, depth_handle, DepthBits::D24, true);

	auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
	uint32_t color = 0xff000000;
	renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
	renderpass_add_depth_attachment(&passBuilder, depth_handle, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_DISCARD);
	renderpass_use_buffer(&passBuilder, ubo_handle);

	struct MainPassPassData
	{
		Application* app;
		HGEGraphics::buffer_handle_t ubo_handle;
	};
	MainPassPassData* passdata;
	renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* passdata)
		{
			MainPassPassData* resolved_passdata = (MainPassPassData*)passdata;
			Application& app = *resolved_passdata->app;
			set_global_texture(encoder, app.color_map, 0, 0);
			set_global_sampler(encoder, app.texture_sampler, 0, 1);
			for (size_t i = 0; i < app.objects.size(); ++i)
			{
				set_global_buffer_with_offset_size(encoder, resolved_passdata->ubo_handle, 0, 2, i * sizeof(ObjectData), sizeof(ObjectData));
				draw(encoder, app.shader, app.meshs[i]);
			}
		}, sizeof(MainPassPassData), (void**)&passdata);
	passdata->app = app;
	passdata->ubo_handle = ubo_handle;
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