#pragma once

#include "cgpu/api.h"
#include <vector>
#include <array>
#include <memory>
#include <optional>
#include "texturepool.h"
#include <functional>
#include "renderer.h"

namespace HGEGraphics
{
	enum class ResourceType
	{
		Texture,
		Buffer
	};

	enum class ManageType
	{
		Managed,
		Imported,
		SubResource,
	};

	enum class DepthBits : uint8_t
	{
		D32 = 32,
		D24 = 24,
		D16 = 16,
	};

	struct alignas(8) ColorAttachmentInfo
	{
		unsigned int clearColor = 0;
		int resourceIndex = 0;
		ECGPULoadAction load_action = CGPU_LOAD_ACTION_DONTCARE;
		ECGPUStoreAction store_action = CGPU_STORE_ACTION_DISCARD;
		bool valid = false;
	};

	struct alignas(8) DepthAttachmentInfo
	{
		float clearDepth = 0;
		uint8_t clearStencil = 0;
		int resourceIndex = 0;
		ECGPULoadAction depth_load_action = CGPU_LOAD_ACTION_DONTCARE;
		ECGPUStoreAction depth_store_action = CGPU_STORE_ACTION_DISCARD;
		ECGPULoadAction stencil_load_action = CGPU_LOAD_ACTION_DONTCARE;
		ECGPUStoreAction stencil_store_action = CGPU_STORE_ACTION_DISCARD;
		bool valid = false;
	};

	class polymorphic_allocator_delete
	{
	public:
		polymorphic_allocator_delete(std::pmr::polymorphic_allocator<std::byte>& allocator)
			: d_allocator(allocator) {}
		template <typename T> void operator()(T* tPtr) 
		{
			std::pmr::polymorphic_allocator<T>(d_allocator).delete_object(tPtr);
		}

	private:
		std::pmr::polymorphic_allocator<std::byte>& d_allocator;
	};

	struct ResourceNode
	{
		ResourceNode();

		const char8_t* name;
		ResourceType resourceType;
		ManageType manageType;
		Texture* texture;
		Buffer* buffer;
		uint16_t width;
		uint16_t height;
		uint16_t depth;
		ECGPUFormat format;
		uint8_t mipCount;;
		uint8_t arraySize;
		uint32_t size;
		uint16_t parent;
		uint8_t mipLevel;
		uint8_t arraySlice;
		CGPUResourceTypes bufferType;
		ECGPUMemoryUsage memoryUsage;
		bool holdOnLast;
	};

	struct RenderGraphEdge
	{
		const uint16_t from;
		const uint16_t to;
		const ECGPUResourceState usage;
	};

	struct RenderPassEncoder;
	typedef void(*renderpass_executable)(RenderPassEncoder* encoder, void* userdata);
	struct UploadEncoder;
	typedef void(*uploadpass_executable)(UploadEncoder* encoder, void* userdata);

	enum pass_type
	{
		PASS_TYPE_HOLDON,
		PASS_TYPE_RENDER,
		PASS_TYPE_UPLOAD_TEXTURE,
		PASS_TYPE_UPLOAD_BUFFER,
		PASS_TYPE_PRESENT,
	};

	struct RenderPassNode
	{
		RenderPassNode(const char8_t* name, pass_type type, std::pmr::memory_resource* const momory_resource);

		const char8_t* name{ nullptr };
		std::pmr::vector<uint32_t> writes;
		std::pmr::vector<uint32_t> reads;
		void* passdata;
		pass_type type;

		struct render_context_t
		{
			int colorAttachmentCount{ 0 };
			std::array<ColorAttachmentInfo, 8> colorAttachments;
			DepthAttachmentInfo depthAttachment;
			renderpass_executable executable;
		};

		struct present_context_t
		{
		};

		struct upload_texture_context_t
		{
			buffer_handle_t staging_buffer;
			resource_handle_t dest_texture;
			uploadpass_executable executable;
			uint64_t size;
			uint64_t offset;
			void* data;
			uint8_t mipmap;
			uint8_t slice;
		};

		struct upload_buffer_context_t
		{
			buffer_handle_t staging_buffer;
			buffer_handle_t dest_buffer;
			uploadpass_executable executable;
			uint64_t size;
			uint64_t offset;
			void* data;
		};

		union
		{
			render_context_t render_context;
			present_context_t present_context;
			upload_texture_context_t upload_texture_context;
			upload_buffer_context_t upload_buffer_context;
		};
	};

	class rendergraph_t;

	struct renderpass_builder_t
	{
		renderpass_builder_t(rendergraph_t* renderGraph, RenderPassNode* passNode, int passIndex);

		rendergraph_t* renderGraph;
		RenderPassNode* passNode;
		uint16_t passIndex;
	};

	void renderpass_add_color_attachment(renderpass_builder_t* self, resource_handle_t texture, ECGPULoadAction load_action, uint32_t clearColor, ECGPUStoreAction store_action);
	void renderpass_add_depth_attachment(renderpass_builder_t* self, resource_handle_t texture, ECGPULoadAction depth_load_action, float clearDepth, ECGPUStoreAction depth_store_action, ECGPULoadAction stencil_load_action, uint8_t clearStencil, ECGPUStoreAction stencil_store_action);
	void renderpass_sample(renderpass_builder_t* self, resource_handle_t texture);
	void renderpass_use_buffer(renderpass_builder_t* self, buffer_handle_t buffer);
	void renderpass_set_executable(renderpass_builder_t* self, renderpass_executable executable, size_t passdata_size, void** passdata);

	void rg_texture_set_extent(rendergraph_t* self, resource_handle_t texture, uint32_t width, uint32_t height, uint32_t depth = 1);
	void rg_texture_set_format(rendergraph_t* self, resource_handle_t texture, ECGPUFormat format);
	void rg_texture_set_depth_format(rendergraph_t* self, resource_handle_t texture, DepthBits depthBits, bool needStencil);
	uint32_t rg_texture_get_width(rendergraph_t* self, resource_handle_t texture);
	uint32_t rg_texture_get_height(rendergraph_t* self, resource_handle_t texture);
	uint32_t rg_texture_get_depth(rendergraph_t* self, resource_handle_t texture);
	ECGPUFormat rg_texture_get_format(rendergraph_t* self, resource_handle_t texture);

	void rg_buffer_set_size(rendergraph_t* self, buffer_handle_t buffer, uint32_t size);
	void rg_buffer_set_type(rendergraph_t* self, buffer_handle_t buffer, ECGPUResourceType type);
	void rg_buffer_set_usage(rendergraph_t* self, buffer_handle_t buffer, ECGPUMemoryUsage usage);
	void rg_buffer_import(rendergraph_t* self, buffer_handle_t buffer, Buffer* imported);
	void rg_buffer_set_hold_on_last(rendergraph_t* self, buffer_handle_t buffer);

	struct rendergraph_t
	{
		using allocator_type = std::pmr::polymorphic_allocator<std::byte>;

		rendergraph_t(size_t estimate_resource_count, size_t estimate_pass_count, size_t estimate_edge_count, Shader* blitShader, CGPUSamplerId blitSampler, std::pmr::memory_resource* const resource);
		std::pmr::vector<ResourceNode> resources;
		std::pmr::vector<RenderPassNode> passes;
		std::pmr::vector<RenderGraphEdge> edges;
		allocator_type allocator;
		Shader* blitShader;
		CGPUSamplerId blitSampler;
	};

	void rendergraph_reset(rendergraph_t* self);
	renderpass_builder_t rendergraph_add_renderpass(rendergraph_t* self, const char8_t* name);
	renderpass_builder_t rendergraph_add_holdpass(rendergraph_t* self, const char8_t* name);
	void rendergraph_add_uploadtexturepass(rendergraph_t* self, const char8_t* name, resource_handle_t texture, uint8_t mipmap, uint8_t slice, uploadpass_executable executable, size_t passdata_size, void** passdata);
	void rendergraph_add_uploadtexturepass_ex(rendergraph_t* self, const char8_t* name, resource_handle_t texture, uint8_t mipmap, uint8_t slice, uint64_t size, uint64_t offset, void* data, uploadpass_executable executable, size_t passdata_size, void** passdata);
	void rendergraph_add_uploadbufferpass(rendergraph_t* self, const char8_t* name, buffer_handle_t buffer, uploadpass_executable executable, size_t passdata_size, void** passdata);
	void rendergraph_add_uploadbufferpass_ex(rendergraph_t* self, const char8_t* name, buffer_handle_t buffer, uint64_t size, uint64_t offset, void* data, uploadpass_executable executable, size_t passdata_size, void** passdata);
	void rendergraph_add_generate_mipmap(rendergraph_t* self, resource_handle_t texture);
	void rendergraph_present(rendergraph_t* self, resource_handle_t texture);
	resource_handle_t rendergraph_declare_texture(rendergraph_t* self);
	resource_handle_t rendergraph_import_texture(rendergraph_t* self, Texture* imported);
	resource_handle_t rendergraph_import_backbuffer(rendergraph_t* self, Backbuffer* imported);
	buffer_handle_t rendergraph_declare_buffer(rendergraph_t* self);
	buffer_handle_t rendergraph_declare_uniform_buffer_quick(rendergraph_t* self, uint32_t size, void* data);
	resource_handle_t rendergraph_declare_texture_subresource(rendergraph_t* self, resource_handle_t parent, uint8_t mipmap, uint8_t slice);
	uint32_t rendergraph_add_edge(rendergraph_t* self, uint32_t from, uint32_t to, ECGPUResourceState usage);

	CGPUBufferId rendergraph_resolve_buffer(RenderPassEncoder* encoder, uint16_t buffer_handle);
	CGPUTextureViewId rendergraph_resolve_texture_view(RenderPassEncoder* encoder, uint16_t texture_handle);
}