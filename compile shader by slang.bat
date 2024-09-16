.\tools\slang\slangc shaders\hello.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o shaders\hello.vert.spv -O3 -emit-spirv-directly
.\tools\slang\slangc shaders\hello.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o shaders\hello.frag.spv -O3 -emit-spirv-directly

.\tools\slang\slangc shaders\light.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o shaders\light.vert.spv -O3 -emit-spirv-directly
.\tools\slang\slangc shaders\light.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o shaders\light.frag.spv -O3 -emit-spirv-directly

.\tools\slang\slangc examples\animation\object.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\animation\object.vert.spv -O3 -emit-spirv-directly -matrix-layout-row-major
.\tools\slang\slangc examples\animation\object.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\animation\object.frag.spv -O3 -emit-spirv-directly -matrix-layout-row-major

.\tools\slang\slangc examples\hdr\skybox.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\hdr\skybox.vert.spv -O3 -emit-spirv-directly -matrix-layout-row-major
.\tools\slang\slangc examples\hdr\skybox.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\hdr\skybox.frag.spv -O3 -emit-spirv-directly -matrix-layout-row-major

.\tools\slang\slangc examples\hdr\unlit.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\hdr\unlit.vert.spv -O3 -emit-spirv-directly -matrix-layout-row-major
.\tools\slang\slangc examples\hdr\unlit.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\hdr\unlit.frag.spv -O3 -emit-spirv-directly -matrix-layout-row-major

.\tools\slang\slangc examples\hdr\hdr.slang -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\hdr\hdr.vert.spv -O3 -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
.\tools\slang\slangc examples\hdr\hdr.slang -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\hdr\hdr.frag.spv -O3 -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary

.\tools\slang\slangc examples\texture3d\texture3d.slang -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\texture3d\texture3d.vert.spv -O3 -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
.\tools\slang\slangc examples\texture3d\texture3d.slang -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\texture3d\texture3d.frag.spv -O3 -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
