add_rules("mode.debug", "mode.release", "mode.releasedbg")
add_cxflags("/EHsc")
set_languages("cxx20", "c11")
if (is_os("windows")) then 
    add_defines("NOMINMAX")
    set_runtimes(is_mode("debug") and "MDd" or "MD")
end

add_requires("libsdl 2.30.7", {configs = {sdlmain = true}})
add_requires("imgui v1.91.1-docking", {configs = {}})

if is_os("windows") or is_os("linux") or is_os("android")  then
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

target("rgdemo")
    set_kind("binary")
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
