#pragma once

#include <unordered_map>
#include <memory_resource>

namespace HGEGraphics
{
	template<typename ResourceDescriptor, typename ResourceType, bool neverRelease, bool destroyOutOfDate, class ResourceDescriptorHasher = std::hash<ResourceDescriptor>, class ResourceDescriptorEq = std::equal_to<ResourceDescriptor>>
	class ResourcePool
	{
		using ThisType = ResourcePool<ResourceDescriptor, ResourceType, neverRelease, destroyOutOfDate, ResourceDescriptorHasher, ResourceDescriptorEq>;
		using MapType = std::pmr::unordered_multimap<ResourceDescriptor, std::pair<ResourceType*, uint64_t>, ResourceDescriptorHasher, ResourceDescriptorEq>;

	public:
		ResourcePool() = default;
		ResourcePool(uint64_t frame_before_out_of_data, ThisType* upstream, std::pmr::memory_resource* const memory_resource)
			: m_upstream(upstream), m_resources(memory_resource), frame_before_out_of_data(frame_before_out_of_data)
		{
		}

		void destroy()
		{
			if (m_upstream)
			{
				for (auto& [key, resource] : m_resources)
				{
					m_upstream->releaseResource(resource.first);
				}
			}
			else
			{
				for (auto& [key, resource] : m_resources)
				{
					destroyResource_impl(resource.first);
				}
			}
			m_resources.clear();
		}

		virtual ~ResourcePool()
		{
		}

		void newFrame()
		{
			++timestamp;

			if constexpr (destroyOutOfDate)
			{
				std::erase_if(m_resources, [this](auto& kv) -> bool 
				{
					bool out_of_date = timestamp > kv.second.second + frame_before_out_of_data;
					if (out_of_date)
					{
						destroyResource_impl(kv.second.first);
					}
					return out_of_date;
				});
			}
		}

		ResourceType* getResource(const ResourceDescriptor& descriptor)
		{
			auto iter = m_resources.find(descriptor);
			if (iter != m_resources.end())
			{
				auto& resourceNode = iter->second;
				auto resource = resourceNode.first;
				if constexpr (!neverRelease)
					m_resources.erase(iter);
				else
				{
					resourceNode.second = timestamp;
				}
				return resource;
			}

			if (m_upstream)
				return m_upstream->getResource(descriptor);
			else
			{
				auto res = getResource_impl(descriptor);
				if constexpr (neverRelease)
					m_resources.insert({ descriptor, {res, timestamp} });
				return res;
			}
		}
		void releaseResource(ResourceType* resource)
		{
			if constexpr (!neverRelease)
				m_resources.insert({ resource->descriptor(), {resource, timestamp} });
		}

		ThisType* upstream() const { return m_upstream; }

	protected:
		virtual ResourceType* getResource_impl(const ResourceDescriptor& descriptor) = 0;
		virtual void destroyResource_impl(ResourceType* resource) = 0;

	protected:
		MapType m_resources;
		ThisType* m_upstream = nullptr;
		uint64_t timestamp = { 0 };
		uint64_t frame_before_out_of_data = { 10 };
	};
}
