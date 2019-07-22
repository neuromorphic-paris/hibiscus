solution 'hibiscus'
    configurations {'release', 'debug'}
    location 'build'
    project 'calibrate'
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        includedirs {'/usr/include/eigen3'}
        files {
            'source/calibrate.cpp',
            'third_party/hummingbird/third_party/glad/src/glad.cpp',
            'third_party/hidapi/hid.c'}
        buildoptions {'-std=c++11'}
        linkoptions {'-std=c++11'}
        links {'glfw', 'dl', 'pthread', 'v4l2', 'jpeg', 'udev', 'ncursesw'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
    project 'record'
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        files {
            'source/record.cpp',
            'third_party/hummingbird/third_party/glad/src/glad.cpp',
            'third_party/hidapi/hid.c'}
        buildoptions {'-std=c++11'}
        linkoptions {'-std=c++11'}
        for path in string.gmatch(
            io.popen('pkg-config --cflags-only-I gstreamermm-1.0'):read('*all'),
            "-I([^%s]+)") do
            includedirs(path)
        end
        linkoptions(io.popen('pkg-config --cflags --libs gstreamermm-1.0'):read('*all'))
        links {'glfw', 'dl', 'pthread', 'udev', 'atomic'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
    project 'monitor_teensy'
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        files {'source/teensy.hpp', 'source/monitor_teensy.cpp'}
        buildoptions {'-std=c++11'}
        linkoptions {'-std=c++11'}
        links {'pthread', 'ncursesw'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
    project 'draw'
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        files {
            'source/draw.cpp',
            'third_party/hummingbird/third_party/glad/src/glad.cpp',
            'third_party/hidapi/hid.c'}
        buildoptions {'-std=c++11'}
        linkoptions {'-std=c++11'}
        links {'glfw', 'dl', 'pthread', 'udev', 'atomic'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
    project 'split'
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        files {'source/split.cpp'}
        buildoptions {'-std=c++11'}
        linkoptions {'-std=c++11'}
        links {'pthread'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
    project 'test'
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        files {
            'source/test.cpp',
            'third_party/hummingbird/third_party/glad/src/glad.cpp',
            'third_party/hidapi/hid.c'}
        buildoptions {'-std=c++11'}
        linkoptions {'-std=c++11'}
        links {'glfw', 'dl', 'pthread', 'udev', 'atomic'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
    project 'monkey_record'
        kind 'ConsoleApp'
        language 'C++'
        location 'build'
        files {
            'source/monkey_record.cpp',
            'third_party/hummingbird/third_party/glad/src/glad.cpp'}
        buildoptions {'-std=c++11'}
        linkoptions {'-std=c++11'}
        for path in string.gmatch(
            io.popen('pkg-config --cflags-only-I gstreamermm-1.0'):read('*all'),
            "-I([^%s]+)") do
            includedirs(path)
        end
        linkoptions(io.popen('pkg-config --cflags --libs gstreamermm-1.0'):read('*all'))
        links {'glfw', 'dl', 'pthread', 'udev', 'atomic'}
        configuration 'release'
            targetdir 'build/release'
            defines {'NDEBUG'}
            flags {'OptimizeSpeed'}
        configuration 'debug'
            targetdir 'build/debug'
            defines {'DEBUG'}
            flags {'Symbols'}
