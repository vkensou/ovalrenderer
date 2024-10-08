.\tools\slang\slangc shaders\hello.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o shaders\hello.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly
.\tools\slang\slangc shaders\hello.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o shaders\hello.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly

.\tools\slang\slangc shaders\light.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o shaders\light.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly
.\tools\slang\slangc shaders\light.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o shaders\light.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly

.\tools\slang\slangc examples\animation\object.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\assets\shaderbin\object.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major
.\tools\slang\slangc examples\animation\object.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\assets\shaderbin\object.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major

.\tools\slang\slangc examples\hdr\skybox.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\assets\shaderbin\skybox.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major
.\tools\slang\slangc examples\hdr\skybox.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\assets\shaderbin\skybox.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major

.\tools\slang\slangc examples\hdr\unlit.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\assets\shaderbin\unlit.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major
.\tools\slang\slangc examples\hdr\unlit.hlsl -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\assets\shaderbin\unlit.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major

.\tools\slang\slangc examples\hdr\hdr.slang -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\assets\shaderbin\hdr.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
.\tools\slang\slangc examples\hdr\hdr.slang -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\assets\shaderbin\hdr.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary

.\tools\slang\slangc examples\texture3d\texture3d.slang -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\assets\shaderbin\texture3d.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
.\tools\slang\slangc examples\texture3d\texture3d.slang -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\assets\shaderbin\texture3d.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary

.\tools\slang\slangc examples\computeparticle\particle.slang -profile sm_5_0 -capability SPIRV_1_3 -entry vert -o examples\assets\shaderbin\particle.vert.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
.\tools\slang\slangc examples\computeparticle\particle.slang -profile sm_5_0 -capability SPIRV_1_3 -entry frag -o examples\assets\shaderbin\particle.frag.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
.\tools\slang\slangc examples\computeparticle\particle_update.slang -profile sm_5_0 -capability SPIRV_1_3 -entry comp -o examples\assets\shaderbin\particle_update.comp.spv -O0 -g3 -line-directive-mode none -emit-spirv-directly -matrix-layout-row-major -I examples\shaderlibrary
