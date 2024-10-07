rule("androidcpp-sdl")
    if is_plat("android") then 
        after_install(function (target)
            local android_sdk_version = target:extraconf("rules", "androidcpp-sdl", "android_sdk_version")
            local android_manifest = target:extraconf("rules", "androidcpp-sdl", "android_manifest")
            local android_res = target:extraconf("rules", "androidcpp-sdl", "android_res")
            local android_assets = target:extraconf("rules", "androidcpp-sdl", "android_assets")
            local keystore = target:extraconf("rules", "androidcpp-sdl", "keystore") or path.join("androidcpp-sdl", "xmake-debug.jks")
            local keystore_pass = target:extraconf("rules", "androidcpp-sdl", "keystore_pass") or "123456"
            local apk_output_path = target:extraconf("rules", "androidcpp-sdl", "apk_output_path") or "."

            assert(android_sdk_version, "android sdk version not set")
            assert(android_manifest, "android manifest not set")
            assert(android_res, "android res not set")
            assert(android_assets, "android assets not set")

            import("android_build")(target, android_sdk_version, android_manifest, android_res, android_assets, keystore, keystore_pass, apk_output_path)
        end)
    end 
