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

const int PARTICLE_COUNT = 256 * 1024;

struct Particle
{
	HMM_Vec2 pos;								// Particle position
	HMM_Vec2 vel;								// Particle velocity
	HMM_Vec4 gradientPos;						// Texture coordinates for the gradient ramp map
};

struct Application
{
	oval_device_t* device;
	CGPUSamplerId sampler = CGPU_NULLPTR;
	clock_t time;
	HGEGraphics::Mesh* quad;
	ObjectData object_data;
	HGEGraphics::Mesh* particle_mesh;
	HGEGraphics::Shader* particle;
	HGEGraphics::Texture* colormap;
	HGEGraphics::Texture* gradientmap;
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
		.depth_test = false,
		.depth_write = false,
		.depth_func = CGPU_CMP_ALWAYS,
		.stencil_test = false,
	};
	CGPURasterizerStateDescriptor rasterizer_state = {
		.cull_mode = CGPU_CULL_MODE_BACK,
	};
	app.particle = HGEGraphics::create_shader(app.device->device, "computeparticle/particle.vert.spv", "computeparticle/particle.frag.spv", blend_desc, depth_desc, rasterizer_state);

	app.colormap = oval_load_texture(app.device, u8"media/textures/particle01_rgba.ktx", false);
	app.gradientmap = oval_load_texture(app.device, u8"media/textures/particle_gradient_rgba.ktx", false);

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

	std::default_random_engine rndEngine((unsigned)time(nullptr));
	std::uniform_real_distribution<float> rndDist(-1.0f, 1.0f);

	std::vector<Particle> init_particles(PARTICLE_COUNT);
	for (auto& particle : init_particles) {
		particle.pos = HMM_V2(rndDist(rndEngine), rndDist(rndEngine));
		particle.vel = HMM_V2(0.0f, 0.0f);
		particle.gradientPos = HMM_V4(particle.pos.X / 2.0f, 0, 0, 0);
	}

	CGPUVertexLayout particle_vertex_layout = 
	{
		.attribute_count = 3,
		.attributes =
		{
			{ u8"POSITION", 1, CGPU_FORMAT_R32G32_SFLOAT, 0, 0, sizeof(float) * 2, CGPU_INPUT_RATE_VERTEX },
			{ u8"TEXCOORD0", 1, CGPU_FORMAT_R32G32_SFLOAT, 0, sizeof(float) * 2, sizeof(float) * 2, CGPU_INPUT_RATE_VERTEX },
			{ u8"TEXCOORD1", 1, CGPU_FORMAT_R32G32B32A32_SFLOAT, 0, sizeof(float) * 4, sizeof(float) * 4, CGPU_INPUT_RATE_VERTEX },
		}
	};
	app.particle_mesh = oval_create_mesh_from_buffer(app.device, PARTICLE_COUNT, 0, CGPU_PRIM_TOPO_POINT_LIST, particle_vertex_layout, 0, (uint8_t*)init_particles.data(), nullptr);
}

void _free_resource(Application& app)
{
	free_texture(app.colormap);
	app.colormap = nullptr;

	free_texture(app.gradientmap);
	app.gradientmap = nullptr;

	free_mesh(app.quad);
	app.quad = nullptr;

	cgpu_free_sampler(app.sampler);
	app.sampler = nullptr;

	free_mesh(app.particle_mesh);
	app.particle_mesh = nullptr;

	free_shader(app.particle);
	app.particle = nullptr;
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

	auto lightRot = HMM_QFromEuler_YXZ(HMM_AngleDeg(0), HMM_AngleDeg(0), 0);
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
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::resource_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	Application* app = (Application*)device->descriptor.userdata;

	auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
	uint32_t color = 0xff000000;
	renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);

	struct MainPassPassData
	{
		Application* app;
	};
	MainPassPassData* passdata;
	renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* passdata)
		{
			MainPassPassData* resolved_passdata = (MainPassPassData*)passdata;
			Application& app = *resolved_passdata->app;

			set_global_texture(encoder, app.colormap, 0, 0);
			set_global_sampler(encoder, app.sampler, 0, 1);
			set_global_texture(encoder, app.gradientmap, 0, 2);
			set_global_sampler(encoder, app.sampler, 0, 3);
			struct ConstantData
			{
				float screendim[2];
			} data;
			data = {
				.screendim = { (float)app.device->descriptor.width, (float)app.device->descriptor.height },
			};
			push_constants(encoder, app.particle, u8"pushConstants", &data);
			draw(encoder, app.particle, app.particle_mesh);
		}, sizeof(MainPassPassData), (void**)&passdata);
	passdata->app = app;
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