#pragma once

#include "cgpu/api.h"
#include <vector>
#include <string>
#include <span>

namespace HGEGraphics
{
	class Profiler
	{
	public:
		Profiler();
		Profiler(CGPUDeviceId device, CGPUQueueId gfx_queue, std::pmr::memory_resource* memory_resource);
		~Profiler();
		void CollectTimings();
		void OnBeginFrame(CGPUCommandBufferId cmd);
		void GetTimeStamp(CGPUCommandBufferId cmd, const char8_t* label);
		void OnEndFrame(CGPUCommandBufferId cmd);
		bool valid() { return labels.size() == durations.size() + 1; }
		void Query(uint32_t& length, const char8_t**& names, const float*& durations);

	private:
		const uint32_t MaxValuesPerFrame = 128;

		double gpuTicksPerSecond;
		CGPUQueryPoolId query_pool = nullptr;
		CGPUBufferId query_buffer = nullptr;
		std::pmr::vector<const char8_t*> labels;
		std::pmr::vector<float> durations;
	};
}