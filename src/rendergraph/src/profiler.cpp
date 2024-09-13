#include "profiler.h"

namespace HGEGraphics
{
	Profiler::Profiler()
	{
	}
	Profiler::Profiler(CGPUDeviceId device, CGPUQueueId gfx_queue, std::pmr::memory_resource* memory_resource)
		: labels(memory_resource)
	{
		gpuTicksPerSecond = cgpu_queue_get_timestamp_period_ns(gfx_queue);
		CGPUQueryPoolDescriptor query_pool_desc = {
			.type = CGPU_QUERY_TYPE_TIMESTAMP,
			.query_count = MaxValuesPerFrame,
		};
		query_pool = cgpu_create_query_pool(device, &query_pool_desc);

		CGPUBufferDescriptor query_buffer_desc = {
		   .size = sizeof(uint64_t) * MaxValuesPerFrame,
		   .name = u8"QueryBuffer",
		   .descriptors = CGPU_RESOURCE_TYPE_NONE,
		   .memory_usage = CGPU_MEM_USAGE_GPU_TO_CPU,
		   .flags = CGPU_BCF_PERSISTENT_MAP_BIT,
		   .start_state = CGPU_RESOURCE_STATE_UNDEFINED,
		};
		query_buffer = cgpu_create_buffer(device, &query_buffer_desc);
	}
	Profiler::~Profiler()
	{
		cgpu_free_buffer(query_buffer);
		cgpu_free_query_pool(query_pool);
	}
	void Profiler::CollectTimings()
	{
		durations.clear();

		uint32_t numMeasurements = (uint32_t)labels.size();
		if (numMeasurements > 0)
		{
			double gpuTicksPerMicroSeconds = gpuTicksPerSecond * 1e-6;

			uint32_t ini = 0;

			auto query_buffer_ptr = std::span<uint64_t>((uint64_t*)query_buffer->info->cpu_mapped_address, labels.size());

			for (uint32_t i = 1; i < numMeasurements; ++i)
			{
				auto last_stamp = query_buffer_ptr[ini + i - 1];
				auto stamp = query_buffer_ptr[ini + i];
				durations.push_back((float)((stamp - last_stamp) * gpuTicksPerMicroSeconds));
			}
		}
	}
	void Profiler::OnBeginFrame(CGPUCommandBufferId cmd)
	{
		labels.clear();
		cgpu_cmd_reset_query_pool(cmd, query_pool, 0, MaxValuesPerFrame);
		GetTimeStamp(cmd, u8"Begin Frame");
	}
	void Profiler::GetTimeStamp(CGPUCommandBufferId cmd, const char8_t* label)
	{
		uint32_t measurements = (uint32_t)labels.size();
		uint32_t offset = measurements;

		CGPUQueryDescriptor query_desc = {
			.index = offset,
			.stage = CGPU_SHADER_STAGE_ALL_GRAPHICS,
		};
		cgpu_cmd_begin_query(cmd, query_pool, &query_desc);

		labels.push_back(label);
	}
	void Profiler::OnEndFrame(CGPUCommandBufferId cmd)
	{
		cgpu_cmd_resolve_query(cmd, query_pool, query_buffer, 0, labels.size());
	}
	void Profiler::Query(uint32_t& length, const char8_t**& names, const float*& durations)
	{
		if (valid())
		{
			length = this->durations.size();
			names = labels.data() + 1;
			durations = this->durations.data();
		}
		else
		{
			length = 0;
			names = nullptr;
			durations = nullptr;
		}
	}
}