add_requires("libsdl", {configs = {sdlmain = true, shared = true}})

target("main")
    set_kind("shared")
    add_packages("libsdl")
    add_files("src/*.cpp")
    add_syslinks("log", "android")
    set_runtimes("c++_shared")