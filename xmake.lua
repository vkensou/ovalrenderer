add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_languages("cxx20", "c11")
if is_plat("windows") then 
    add_defines("NOMINMAX")
    set_runtimes(is_mode("debug") and "MDd" or "MD")
    add_cxflags("/EHsc")
    add_ldflags("-subsystem:console")
elseif is_plat("android") then
    add_cxflags("-fPIC")
    includes("androidcpp-sdl")
    set_runtimes("c++_static")
end

add_requires("libsdl 2.30.7", {configs = {sdlmain = true, shared = true}})
add_requires("imgui v1.91.1-docking", {configs = {}})

if is_plat("windows", "linux", "android") then
    option("use_vulkan")
        set_showmenu(true )
        set_default(true)
end

includes("cgpu/xmake.lua")

target("rendergraph")
    set_kind("static")
    add_deps("cgpu")
    add_includedirs("src/rendergraph/include", {public = true})
    add_headerfiles("src/rendergraph/include/*.h")
    add_headerfiles("src/rendergraph/src/*.h", {install = false})
    add_files("src/rendergraph/src/*.cpp")

includes("src/khr/xmake.lua")

target("rgframework")
    set_kind("static")
    add_deps("cgpu")
    add_deps("rendergraph")
    add_deps("ktx")
    add_defines("KHRONOS_STATIC")
    add_packages("libsdl")
    add_packages("imgui", {public = true})
    add_rules("utils.hlsl2spv", {bin2c = true})
    add_includedirs("src/rgframework/include", {public = true})
    add_headerfiles("src/rgframework/include/*.h")
    add_files("src/rgframework/src/*.cpp")
    add_files("src/rgframework/src/*.hlsl")
    if is_plat("windows") then 
        add_syslinks("Advapi32")
    end 

target("rgdemo")
    if is_plat("windows") then
        set_kind("binary")
        add_ldflags("/subsystem:console")
    else 
        set_kind("shared")
        add_rules("androidcpp-sdl", {android_sdk_version = "34", android_manifest = "AndroidManifest.xml", android_res = "res", android_assets = "assets", apk_output_path = "."})
    end
    set_rundir("$(projectdir)")
    add_deps("rgframework")
    add_files("src/rgdemo/*.cpp")

target("animation")
    set_group("examples")
    set_kind("binary")
    set_rundir("$(projectdir)/examples")
    add_deps("rgframework")
    add_files("examples/animation/*.cpp")

target("hdr")
    set_group("examples")
    set_kind("binary")
    set_rundir("$(projectdir)/examples")
    add_deps("rgframework")
    add_files("examples/hdr/*.cpp")

target("texture3d")
    set_group("examples")
    set_kind("binary")
    set_rundir("$(projectdir)/examples")
    add_deps("rgframework")
    add_files("examples/texture3d/*.cpp")

target("computeparticle")
    set_group("examples")
    set_kind("binary")
    set_rundir("$(projectdir)/examples")
    add_deps("rgframework")
    add_files("examples/computeparticle/*.cpp")
