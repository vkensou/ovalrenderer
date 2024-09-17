.\tools\dxc\dxc shaders\hello.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo shaders\hello.vert.spv -Od -Zi
.\tools\dxc\dxc shaders\hello.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo shaders\hello.frag.spv -Od -Zi

.\tools\dxc\dxc shaders\light.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo shaders\light.vert.spv -Od -Zi
.\tools\dxc\dxc shaders\light.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo shaders\light.frag.spv -Od -Zi

.\tools\dxc\dxc examples\animation\object.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo examples\animation\object.vert.spv -Od -Zi -Zpr
.\tools\dxc\dxc examples\animation\object.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo examples\animation\object.frag.spv -Od -Zi -Zpr

.\tools\dxc\dxc examples\hdr\skybox.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo examples\hdr\skybox.vert.spv -Od -Zi -Zpr
.\tools\dxc\dxc examples\hdr\skybox.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo examples\hdr\skybox.frag.spv -Od -Zi -Zpr

.\tools\dxc\dxc examples\hdr\unlit.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo examples\hdr\unlit.vert.spv -Od -Zi -Zpr
.\tools\dxc\dxc examples\hdr\unlit.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo examples\hdr\unlit.frag.spv -Od -Zi -Zpr

.\tools\dxc\dxc examples\texture3d\texture3d.hlsl -spirv -T vs_6_0 -E vert -fspv-entrypoint-name=main -Fo examples\texture3d\texture3d.vert.spv -Od -Zi -Zpr
.\tools\dxc\dxc examples\texture3d\texture3d.hlsl -spirv -T ps_6_0 -E frag -fspv-entrypoint-name=main -Fo examples\texture3d\texture3d.frag.spv -Od -Zi -Zpr
