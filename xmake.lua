add_rules("mode.debug", "mode.release")

-- 1. cmake add option to disable warning in vs
-- target_compile_options(shuio PUBLIC /wd4819)

-- 2. xmake project -k cmake
-- create cmakelist.txt; cmake 插件比 xmake 好用很多

set_languages("c++20")

-- ASAN_SYMBOLIZER_PATH=$(which llvm-symbolizer) ./bench_http_wrk
if is_mode("release") and not is_os("windows") then
    -- set_optimize("faster")
    add_cxxflags("-g")
    add_cxxflags("-fsanitize=address")
    add_cxxflags("-fno-omit-frame-pointer")
end

target("shuio")
    set_kind("static")
    add_includedirs("$(projectdir)", {public = true})
    if is_os("windows") then
        add_files("shuio/win32/*.cpp")
    elseif is_os("linux") then
        add_links("asan")
        add_links("uring")
        add_files("shuio/linux/*.cpp")
    else
        assert(false, "not support")
    end

target("example")
    set_kind("binary")
    add_deps("shuio")
    add_files("example/sio.cpp")

target("pingpong_server")
    set_kind("binary")
    add_deps("shuio")
    add_files("example/pingpong/server.cpp")

target("pingpong_client")
    set_kind("binary")
    add_deps("shuio")
    add_files("example/pingpong/client.cpp")

-- wrk -t4 -c1000 -d30s --latency http://127.0.0.1:8888
target("bench_http_wrk")
    set_kind("binary")
    if is_os("linux") then
        add_links("asan")
    end
    add_deps("shuio")
    add_files("example/bench_http_wrk/*.cpp")

--
-- If you want to known more usage about xmake, please see https://xmake.io
--
-- ## FAQ
--
-- You can enter the project directory firstly before building project.
--
--   $ cd projectdir
--
-- 1. How to build project?
--
--   $ xmake
--
-- 2. How to configure project?
--
--   $ xmake f -p [macosx|linux|iphoneos ..] -a [x86_64|i386|arm64 ..] -m [debug|release]
--
-- 3. Where is the build output directory?
--
--   The default output directory is `./build` and you can configure the output directory.
--
--   $ xmake f -o outputdir
--   $ xmake
--
-- 4. How to run and debug target after building project?
--
--   $ xmake run [targetname]
--   $ xmake run -d [targetname]
--
-- 5. How to install target to the system directory or other output directory?
--
--   $ xmake install
--   $ xmake install -o installdir
--
-- 6. Add some frequently-used compilation flags in xmake.lua
--
-- @code
--    -- add debug and release modes
--    add_rules("mode.debug", "mode.release")
--
--    -- add macro defination
--    add_defines("NDEBUG", "_GNU_SOURCE=1")
--
--    -- set warning all as error
--    set_warnings("all", "error")
--
--    -- set language: c99, c++11
--    set_languages("c99", "c++11")
--
--    -- set optimization: none, faster, fastest, smallest
--    set_optimize("fastest")
--
--    -- add include search directories
--    add_includedirs("/usr/include", "/usr/local/include")
--
--    -- add link libraries and search directories
--    add_links("tbox")
--    add_linkdirs("/usr/local/lib", "/usr/lib")
--
--    -- add system link libraries
--    add_syslinks("z", "pthread")
--
--    -- add compilation and link flags
--    add_cxflags("-stdnolib", "-fno-strict-aliasing")
--    add_ldflags("-L/usr/local/lib", "-lpthread", {force = true})
--
-- @endcode
--

