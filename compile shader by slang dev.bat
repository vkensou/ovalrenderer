.\tools\slang\slangc shaders\hello.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o shaders\hello.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly
.\tools\slang\slangc shaders\hello.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o shaders\hello.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly

.\tools\slang\slangc shaders\light.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o shaders\light.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly
.\tools\slang\slangc shaders\light.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o shaders\light.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly

.\tools\slang\slangc examples\animation\object.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\animation\object.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major
.\tools\slang\slangc examples\animation\object.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\animation\object.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major
