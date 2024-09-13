#include "framework.h"
#include "imgui.h"

struct Application
{
	oval_device_t* device;
	HGEGraphics::Texture* cubemap;
};

void _init_resource(Application& app)
{
	app.cubemap = oval_load_texture(app.device, u8"media/textures/uffizi_cube.ktx", true);
}

void _free_resource(Application& app)
{
	free_texture(app.cubemap);
	app.cubemap = nullptr;
}

void _init_world(Application& app)
{
}

void on_update(oval_device_t* device)
{
}

void on_imgui(oval_device_t* device)
{
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::resource_handle_t rg_back_buffer)
{
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