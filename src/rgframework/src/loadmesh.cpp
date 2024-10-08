#include "cgpu_device.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

std::tuple<std::pmr::vector<TexturedVertex>*, std::pmr::vector<uint32_t>*> LoadObjModel(const char8_t* filename, bool right_hand, std::pmr::memory_resource* memory_resource)
{
	tinyobj::attrib_t attrib;
	std::vector<tinyobj::shape_t> shapes;
	std::vector<tinyobj::material_t> materials;
	std::string warn, err;

	std::pmr::vector<TexturedVertex>* vertices;
	std::pmr::vector<uint32_t>* indices;
	std::u8string finalpath = std::u8string(u8"assets/") + filename;
	if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err, (const char*)finalpath.c_str()))
	{
		return { vertices, indices };
	}

	vertices = new std::pmr::vector<TexturedVertex>(memory_resource);
	indices = new std::pmr::vector<uint32_t>(memory_resource);

	std::pmr::vector<HMM_Vec3> coords(memory_resource), normals(memory_resource);
	std::pmr::vector<HMM_Vec2> texCoords(memory_resource);
	int rh = right_hand ? -1 : 1;

	coords.reserve(attrib.vertices.size() / 3);
	for (size_t i = 0; i < attrib.vertices.size() / 3; ++i)
	{
		coords.push_back({ attrib.vertices[3 * i + 0] * rh, attrib.vertices[3 * i + 1], attrib.vertices[3 * i + 2] });
	}

	normals.reserve(attrib.normals.size() / 3);
	for (size_t i = 0; i < attrib.normals.size() / 3; ++i)
	{
		normals.push_back({ attrib.normals[3 * i + 0] * rh, attrib.normals[3 * i + 1], attrib.normals[3 * i + 2] });
	}

	texCoords.reserve(attrib.texcoords.size() / 2);
	for (size_t i = 0; i < attrib.texcoords.size() / 2; ++i)
	{
		texCoords.push_back({ attrib.texcoords[2 * i + 0], 1 - attrib.texcoords[2 * i + 1] });
	}

	struct TripleIntHasher
	{
		size_t operator()(const std::tuple<int, int, int>& t) const {
			size_t h1 = std::hash<int>{}(get<0>(t));
			size_t h2 = std::hash<int>{}(get<1>(t));
			size_t h3 = std::hash<int>{}(get<2>(t));
			return ((h1 ^ (h2 << 1)) >> 1) ^ h3;
		}
	};
	std::unordered_map<std::tuple<int, int, int>, uint32_t, TripleIntHasher> vertex_map;

	vertices->reserve(coords.size());
	for (size_t i = 0; i < shapes.size(); ++i)
	{
		auto& mesh = shapes[i].mesh;
		indices->reserve(mesh.indices.size());
		for (size_t j = 0; j < mesh.indices.size(); ++j)
		{
			int vertex_index = mesh.indices[j].vertex_index;
			int normal_index = mesh.indices[j].normal_index;
			int texcoord_index = mesh.indices[j].texcoord_index;

			auto vertex_map_index = std::tuple{ vertex_index, normal_index, texcoord_index };
			auto iter = vertex_map.find(vertex_map_index);
			if (iter != vertex_map.end())
			{
				auto vertex_index = iter->second;
				indices->push_back(vertex_index);
			}
			else
			{
				auto pos = coords[vertex_index];
				auto normal = normal_index >= 0 ? normals[normal_index] : HMM_Vec3();
				auto texcoord = texcoord_index >= 0 ? texCoords[texcoord_index] : HMM_Vec2();
				auto iter = vertex_map.insert({ vertex_map_index , vertices->size() });
				vertices->push_back({ pos, normal, texcoord });
				indices->push_back(iter.first->second);
			}
		}

		break;
	}

	return { vertices, indices };
}

uint64_t load_mesh(oval_cgpu_device_t* device, oval_graphics_transfer_queue_t queue, HGEGraphics::Mesh* mesh, const char8_t* filepath)
{
	auto [data, indices] = LoadObjModel(filepath, true, device->memory_resource);

	if (!data)
	{
		if (indices)
			delete indices;
		return 0;
	}

	CGPUVertexLayout mesh_vertex_layout =
	{
		.attribute_count = 3,
		.attributes =
		{
			{ u8"POSITION", 1, CGPU_FORMAT_R32G32B32_SFLOAT, 0, 0, sizeof(float) * 3, CGPU_INPUT_RATE_VERTEX },
			{ u8"NORMAL", 1, CGPU_FORMAT_R32G32B32_SFLOAT, 0, sizeof(float) * 3, sizeof(float) * 3, CGPU_INPUT_RATE_VERTEX },
			{ u8"TEXCOORD", 1, CGPU_FORMAT_R32G32_SFLOAT, 0, sizeof(float) * 6, sizeof(float) * 2, CGPU_INPUT_RATE_VERTEX },
		}
	};

	HGEGraphics::init_mesh(mesh, device->device, data->size(), (indices ? indices->size() : 0), CGPU_PRIM_TOPO_TRI_LIST, mesh_vertex_layout, (indices ? sizeof(uint32_t) : 0), false, false);

	uint64_t vertex_data_size = mesh->vertices_count * mesh->vertex_stride;
	auto vertex_data = oval_graphics_transfer_queue_transfer_data_to_buffer(queue, vertex_data_size, mesh->vertex_buffer);
	memcpy(vertex_data, data->data(), vertex_data_size);

	uint64_t index_data_size = mesh->index_count * mesh->index_stride;
	if (indices)
	{
		auto index_data = oval_graphics_transfer_queue_transfer_data_to_buffer(queue, index_data_size, mesh->index_buffer);
		memcpy(index_data, indices->data(), index_data_size);
	}

	delete data;
	delete indices;

	return vertex_data_size + index_data_size;
}
