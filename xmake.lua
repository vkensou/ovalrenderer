add_rules("mode.debug", "mode.release", "mode.releasedbg")
set_languages("cxx20", "c11")
if is_plat("windows") then 
    add_defines("NOMINMAX")
    set_runtimes(is_mode("debug") and "MDd" or "MD")
    add_cxflags("/EHsc")
    add_ldflags("-subsystem:console")
elseif is_plat("android") then
    add_cxflags("-fPIC")
    includes("androidcpp")
    set_runtimes("c++_static")
end

if is_host("windows") and is_plat("android") then
    set_policy("install.strip_packagelibs", false)
end

add_requires("libsdl 2.30.7", {configs = {sdlmain = true, shared = false}})
add_requires("imgui v1.91.1-docking")
add_requires("tbox v1.7.6")

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
    add_packages("tbox", {public = true})
    add_rules("utils.hlsl2spv", {bin2c = true})
    add_includedirs("src/rgframework/include", {public = true})
    add_headerfiles("src/rgframework/include/*.h")
    add_files("src/rgframework/src/*.cpp")
    add_files("src/rgframework/src/*.hlsl")
    if is_plat("windows") then 
        add_syslinks("Advapi32")
    end 

rule("example_base")
    after_load(function(target)
        target:set("group", "examples")
        if is_plat("android") then
            target:set("kind", "shared")
        else 
            target:set("kind", "binary")
            if is_plat("windows") then
                target:add("ldflags", "/subsystem:console")
            end
        end
        target:set("rundir", "$(projectdir)/examples/assets")
        target:add("deps", "rgframework")
    end)

target("rgdemo")
    add_rules("example_base")
    if is_plat("android") then
        add_rules("androidcpp", {android_sdk_version = "34", android_manifest = "AndroidManifest.xml", android_res = "res", android_assets = "assets", attachedjar = path.join("androidsdl", "libsdl-2.30.7.jar"), apk_output_path = ".", package_name = "com.xmake.androidcpp", activity_name = "org.libsdl.app.SDLActivity"})
    end
    add_files("src/rgdemo/*.cpp")

target("animation")
    add_rules("example_base")
    if is_plat("android") then
        add_rules("androidcpp", {android_sdk_version = "34", android_manifest = "examples/AndroidManifest.xml", android_res = "examples/res", android_assets = "examples/assets", attachedjar = path.join("androidsdl", "libsdl-2.30.7.jar"), apk_output_path = ".", package_name = "com.xmake.androidcpp", activity_name = "org.libsdl.app.SDLActivity"})
    end
    add_files("examples/animation/*.cpp")

target("hdr")
    add_rules("example_base")
    if is_plat("android") then
        add_rules("androidcpp", {android_sdk_version = "34", android_manifest = "examples/AndroidManifest.xml", android_res = "examples/res", android_assets = "examples/assets", attachedjar = path.join("androidsdl", "libsdl-2.30.7.jar"), apk_output_path = ".", package_name = "com.xmake.androidcpp", activity_name = "org.libsdl.app.SDLActivity"})
    end
    add_files("examples/hdr/*.cpp")

target("texture3d")
    add_rules("example_base")
    if is_plat("android") then
        add_rules("androidcpp", {android_sdk_version = "34", android_manifest = "examples/AndroidManifest.xml", android_res = "examples/res", android_assets = "examples/assets", attachedjar = path.join("androidsdl", "libsdl-2.30.7.jar"), apk_output_path = ".", package_name = "com.xmake.androidcpp", activity_name = "org.libsdl.app.SDLActivity"})
    end
    add_files("examples/texture3d/*.cpp")

target("computeparticle")
    add_rules("example_base")
    if is_plat("android") then
        add_rules("androidcpp", {android_sdk_version = "34", android_manifest = "examples/AndroidManifest.xml", android_res = "examples/res", android_assets = "examples/assets", attachedjar = path.join("androidsdl", "libsdl-2.30.7.jar"), apk_output_path = ".", package_name = "com.xmake.androidcpp", activity_name = "org.libsdl.app.SDLActivity"})
    end
    add_files("examples/computeparticle/*.cpp")
