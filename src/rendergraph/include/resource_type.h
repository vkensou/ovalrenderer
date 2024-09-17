#pragma once

#include <stdint.h>

namespace HGEGraphics
{
	typedef uint32_t index_type_t;
	const index_type_t MAX_INDEX = UINT32_MAX - 2;

	struct texture_handle_t
	{
		index_type_t index;
	};

	struct buffer_handle_t
	{
		index_type_t index;
	};
}