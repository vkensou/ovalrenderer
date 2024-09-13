#pragma once

#include "rendergraph_compiler.h"
#include "renderer.h"

namespace HGEGraphics
{
	struct Executor
	{
		static void Execute(CompiledRenderGraph& compiledRenderGraph, ExecutorContext& texturepool);
	};
}