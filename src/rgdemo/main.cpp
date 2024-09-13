#include "framework.h"
#include "imgui.h"

struct Application
{
	oval_device_t* device;
	HGEGraphics::Shader* shader;
	HGEGraphics::Shader* light_shader;
	HGEGraphics::resource_handle_t gbuffer;
	CGPUSamplerId gbuffer_sampler = CGPU_NULLPTR;
};

void _init(Application& app)
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
		.stencil_test = false,
	};
	CGPURasterizerStateDescriptor rasterizer_state = {
		.cull_mode = CGPU_CULL_MODE_NONE,
	};
	app.shader = HGEGraphics::create_shader(app.device->device, "shaders/hello.vert.spv", "shaders/hello.frag.spv", blend_desc, depth_desc, rasterizer_state);
	app.light_shader = HGEGraphics::create_shader(app.device->device, "shaders/light.vert.spv", "shaders/light.frag.spv", blend_desc, depth_desc, rasterizer_state);

	CGPUSamplerDescriptor gbuffer_sampler_desc = {
		.min_filter = CGPU_FILTER_TYPE_LINEAR,
		.mag_filter = CGPU_FILTER_TYPE_LINEAR,
		.mipmap_mode = CGPU_MIPMAP_MODE_LINEAR,
		.address_u = CGPU_ADDRESS_MODE_CLAMP_TO_EDGE,
		.address_v = CGPU_ADDRESS_MODE_CLAMP_TO_EDGE,
		.address_w = CGPU_ADDRESS_MODE_CLAMP_TO_EDGE,
		.mip_lod_bias = 0,
		.max_anisotropy = 1,
	};
	app.gbuffer_sampler = cgpu_create_sampler(app.device->device, &gbuffer_sampler_desc);
}

void _free(Application& app)
{
	free_shader(app.shader);
	app.shader = nullptr;

	free_shader(app.light_shader);
	app.light_shader = nullptr;

	cgpu_free_sampler(app.gbuffer_sampler);
	app.gbuffer_sampler = nullptr;
}

void on_update(oval_device_t* device)
{

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

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::resource_handle_t rg_back_buffer)
{
	using namespace HGEGraphics;

	Application* app = (Application*)device->descriptor.userdata;

	auto gbuffer = rendergraph_declare_texture(&rg);
	rg_texture_set_extent(&rg, gbuffer, rg_texture_get_width(&rg, rg_back_buffer), rg_texture_get_height(&rg, rg_back_buffer));
	rg_texture_set_format(&rg, gbuffer, CGPU_FORMAT_R8G8B8A8_UNORM);

	auto gPassBuilder = rendergraph_add_renderpass(&rg, u8"GPass");

	renderpass_add_color_attachment(&gPassBuilder, gbuffer, CGPU_LOAD_ACTION_CLEAR, 0, CGPU_STORE_ACTION_STORE);
	struct GBufferPassData
	{
		Application* app;
	};
	GBufferPassData* passdata;
	renderpass_set_executable(&gPassBuilder, [](RenderPassEncoder* encoder, void* userdata)
		{
			Application* app = ((GBufferPassData*)userdata)->app;
			draw_procedure(encoder, app->shader, CGPU_PRIM_TOPO_TRI_LIST, 3);
		}, sizeof(GBufferPassData), (void**)&passdata);
	passdata->app = app;

	auto passBuilder = rendergraph_add_renderpass(&rg, u8"Main Pass");
	uint32_t color = 0xffffffff;
	renderpass_add_color_attachment(&passBuilder, rg_back_buffer, ECGPULoadAction::CGPU_LOAD_ACTION_CLEAR, color, ECGPUStoreAction::CGPU_STORE_ACTION_STORE);
	renderpass_sample(&passBuilder, gbuffer);
	app->gbuffer = gbuffer;

	struct MainPassData
	{
		Application* app;
	};
	MainPassData* passdata2;
	renderpass_set_executable(&passBuilder, [](RenderPassEncoder* encoder, void* userdata)
		{
			Application* app = ((GBufferPassData*)userdata)->app;
			set_global_texture_handle(encoder, app->gbuffer, 0, 0);
			set_global_sampler(encoder, app->gbuffer_sampler, 0, 1);
			draw_procedure(encoder, app->light_shader, CGPU_PRIM_TOPO_TRI_LIST, 3);
		}, sizeof(MainPassData), (void**)&passdata2);
	passdata2->app = app;
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
		.enable_capture = true,
		.enable_profile = true,
	};
	app.device = oval_create_device(&device_descriptor);
	_init(app);
	if (app.device)
	{
		oval_runloop(app.device);
		_free(app);
		oval_free_device(app.device);
	}

	return 0;
}