.\tools\dxc\dxc shaders\hello.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo shaders\hello.vert.spv -Od -Zi
.\tools\dxc\dxc shaders\hello.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo shaders\hello.frag.spv -Od -Zi

.\tools\dxc\dxc shaders\light.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo shaders\light.vert.spv -Od -Zi
.\tools\dxc\dxc shaders\light.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo shaders\light.frag.spv -Od -Zi

.\tools\dxc\dxc examples\animation\object.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo examples\animation\object.vert.spv -Od -Zi -Zpr
.\tools\dxc\dxc examples\animation\object.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo examples\animation\object.frag.spv -Od -Zi -Zpr
