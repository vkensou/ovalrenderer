#include "framework.h"
#include "imgui.h"
#include <random>
#include <numeric>

// Translation of Ken Perlin's JAVA implementation (http://mrl.nyu.edu/~perlin/noise/)
template <typename T>
class PerlinNoise
{
private:
	uint32_t permutations[512];
	T fade(T t)
	{
		return t * t * t * (t * (t * (T)6 - (T)15) + (T)10);
	}
	T lerp(T t, T a, T b)
	{
		return a + t * (b - a);
	}
	T grad(int hash, T x, T y, T z)
	{
		// Convert LO 4 bits of hash code into 12 gradient directions
		int h = hash & 15;
		T u = h < 8 ? x : y;
		T v = h < 4 ? y : h == 12 || h == 14 ? x : z;
		return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
	}
public:
	PerlinNoise(bool applyRandomSeed)
	{
		// Generate random lookup for permutations containing all numbers from 0..255
		std::vector<uint8_t> plookup;
		plookup.resize(256);
		std::iota(plookup.begin(), plookup.end(), 0);
		std::default_random_engine rndEngine(applyRandomSeed ? std::random_device{}() : 0);
		std::shuffle(plookup.begin(), plookup.end(), rndEngine);

		for (uint32_t i = 0; i < 256; i++)
		{
			permutations[i] = permutations[256 + i] = plookup[i];
		}
	}
	T noise(T x, T y, T z)
	{
		// Find unit cube that contains point
		int32_t X = (int32_t)floor(x) & 255;
		int32_t Y = (int32_t)floor(y) & 255;
		int32_t Z = (int32_t)floor(z) & 255;
		// Find relative x,y,z of point in cube
		x -= floor(x);
		y -= floor(y);
		z -= floor(z);

		// Compute fade curves for each of x,y,z
		T u = fade(x);
		T v = fade(y);
		T w = fade(z);

		// Hash coordinates of the 8 cube corners
		uint32_t A = permutations[X] + Y;
		uint32_t AA = permutations[A] + Z;
		uint32_t AB = permutations[A + 1] + Z;
		uint32_t B = permutations[X + 1] + Y;
		uint32_t BA = permutations[B] + Z;
		uint32_t BB = permutations[B + 1] + Z;

		// And add blended results for 8 corners of the cube;
		T res = lerp(w, lerp(v,
			lerp(u, grad(permutations[AA], x, y, z), grad(permutations[BA], x - 1, y, z)), lerp(u, grad(permutations[AB], x, y - 1, z), grad(permutations[BB], x - 1, y - 1, z))),
			lerp(v, lerp(u, grad(permutations[AA + 1], x, y, z - 1), grad(permutations[BA + 1], x - 1, y, z - 1)), lerp(u, grad(permutations[AB + 1], x, y - 1, z - 1), grad(permutations[BB + 1], x - 1, y - 1, z - 1))));
		return res;
	}
};

// Fractal noise generator based on perlin noise above
template <typename T>
class FractalNoise
{
private:
	PerlinNoise<T> perlinNoise;
	uint32_t octaves;
	T frequency;
	T amplitude;
	T persistence;
public:
	FractalNoise(const PerlinNoise<T>& perlinNoiseIn) :
		perlinNoise(perlinNoiseIn)
	{
		octaves = 6;
		persistence = (T)0.5;
	}

	T noise(T x, T y, T z)
	{
		T sum = 0;
		T frequency = (T)1;
		T amplitude = (T)1;
		T max = (T)0;
		for (uint32_t i = 0; i < octaves; i++)
		{
			sum += perlinNoise.noise(x * frequency, y * frequency, z * frequency) * amplitude;
			max += amplitude;
			amplitude *= persistence;
			frequency *= (T)2;
		}

		sum = sum / max;
		return (sum + (T)1.0) / (T)2.0;
	}
};

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
	HGEGraphics::Shader* texture3d;
	HGEGraphics::Texture* noisemap;
	CGPUSamplerId sampler = CGPU_NULLPTR;
	clock_t time;
	HGEGraphics::Mesh* quad;
	ObjectData object_data;
	float lightDirEulerX, lightDirEulerY;
};

void initNoiseMap(Application& app, uint32_t width, uint32_t height, uint32_t depth)
{
	const uint32_t texMemSize = width * height * depth;

	CGPUTextureDescriptor descriptor =
	{
		.width = width,
		.height = height,
		.depth = depth,
		.array_size = 1,
		.format = CGPU_FORMAT_R8_UNORM,
		.mip_levels = 1,
		.descriptors = CGPU_RESOURCE_TYPE_TEXTURE,
	};
	app.noisemap = oval_create_texture(app.device, descriptor);
	uint8_t* data = oval_graphics_set_texture_data_slice(app.device, app.noisemap, 0, 0, nullptr);

	PerlinNoise<float> perlinNoise(true);
	FractalNoise<float> fractalNoise(perlinNoise);

	const float noiseScale = static_cast<float>(rand() % 10) + 4.0f;

	for (int32_t z = 0; z < static_cast<int32_t>(depth); z++)
	{
		for (int32_t y = 0; y < static_cast<int32_t>(height); y++)
		{
			for (int32_t x = 0; x < static_cast<int32_t>(width); x++)
			{
				float nx = (float)x / (float)width;
				float ny = (float)y / (float)height;
				float nz = (float)z / (float)depth;
				float n = fractalNoise.noise(nx * noiseScale, ny * noiseScale, nz * noiseScale);
				n = n - floor(n);
				data[x + y * width + z * width * height] = static_cast<uint8_t>(floor(n * 255));
			}
		}
	}
}

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

	app.texture3d = oval_create_shader(app.device, "texture3d/texture3d.vert.spv", "texture3d/texture3d.frag.spv", blend_desc, depth_desc, rasterizer_state);

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
	app.sampler = oval_create_sampler(app.device, &cubemap_sampler_desc);

	app.quad = oval_load_mesh(app.device, u8"media/models/Quad.obj");

	initNoiseMap(app, 128, 128, 128);
}

void _free_resource(Application& app)
{
	oval_free_mesh(app.device, app.quad);
	app.quad = nullptr;

	oval_free_sampler(app.device, app.sampler);
	app.sampler = nullptr;

	oval_free_texture(app.device, app.noisemap);
	app.noisemap = nullptr;

	oval_free_shader(app.device, app.texture3d);
	app.texture3d = nullptr;
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
	ImGui::SliderFloat2("Light Dir", &app->lightDirEulerX, -360, 360);
}

void on_draw(oval_device_t* device, HGEGraphics::rendergraph_t& rg, HGEGraphics::texture_handle_t rg_back_buffer)
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

			set_global_texture(encoder, app.noisemap, 0, 0);
			set_global_sampler(encoder, app.sampler, 0, 1);
			set_global_buffer(encoder, resolved_passdata->object_ubo_handle, 0, 2);
			draw(encoder, app.texture3d, app.quad);
		}, sizeof(MainPassPassData), (void**)&passdata);
	passdata->app = app;
	passdata->object_ubo_handle = object_ubo_handle;
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