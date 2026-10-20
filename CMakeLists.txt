cmake_minimum_required(VERSION 3.21)
option(DOKODEMO_ALLOW_EXTERNAL_BUILD "Allow an explicitly selected build directory on another volume" OFF)
cmake_path(IS_PREFIX CMAKE_SOURCE_DIR "${CMAKE_BINARY_DIR}" NORMALIZE build_inside)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR OR (NOT build_inside AND NOT DOKODEMO_ALLOW_EXTERNAL_BUILD))
    message(FATAL_ERROR "Use an out-of-source build: -B build/cmake, or explicitly set DOKODEMO_ALLOW_EXTERNAL_BUILD=ON for another volume.")
endif()
project(dokodemo_native LANGUAGES CXX)

option(DOKODEMO_BUILD_WINDOWS_APP "Build the Win32 recovery stand" ${WIN32})
set(DOKODEMO_FFMPEG_ROOT "" CACHE PATH "Optional pinned FFmpeg 7.1.1 Windows shared dependency root for prepared codec envelopes")
if(DOKODEMO_FFMPEG_ROOT)
    if(NOT WIN32)
        message(FATAL_ERROR "The prepared codec envelope backend currently requires Windows.")
    endif()
    cmake_path(ABSOLUTE_PATH DOKODEMO_FFMPEG_ROOT NORMALIZE OUTPUT_VARIABLE prepared_codec_root)
    foreach(codec_input include/libavcodec/avcodec.h include/libavutil/channel_layout.h bin/avcodec-61.dll bin/avutil-59.dll bin/swresample-5.dll)
        if(NOT EXISTS "${prepared_codec_root}/${codec_input}")
            message(FATAL_ERROR "Missing pinned prepared codec input: ${codec_input}")
        endif()
    endforeach()
endif()
function(dokodemo_configure_prepared_codec target)
    if(DOKODEMO_FFMPEG_ROOT)
        target_include_directories(${target} SYSTEM PRIVATE "${prepared_codec_root}/include")
        target_compile_definitions(${target} PRIVATE DOKODEMO_FFMPEG_ROOT="${prepared_codec_root}")
    endif()
endfunction()

# The core contains game-specific recovered C++, resource parsers and the
# bounded KSC interpreter. Core sources use no Windows APIs or PSP CPU/OS runtime.
add_library(dokodemo-core STATIC
    src/core_hash.cpp
    src/core/cp932.cpp
    src/script_vm.cpp
    src/startup_probe.cpp
    src/recovered/startup_flow_native.cpp
    src/recovered/audiocodec_module_native.cpp
    src/recovered/module_manager_native.cpp
    src/recovered/atrac3plus_module_native.cpp
    src/recovered/sascore_module_native.cpp
    src/recovered/ifhandle_module_native.cpp
    src/recovered/pspnet_module_native.cpp
    src/recovered/memab_module_native.cpp
    src/recovered/adhoc_auth_module_native.cpp
    src/recovered/adhoc_module_native.cpp
    src/recovered/adhocctl_module_native.cpp
    src/model_formats.cpp
    src/recovered/game_primitives.cpp
    src/recovered/menu_primitives.cpp
    src/recovered/calendar_native.cpp
    src/recovered/service47_native.cpp
    src/recovered/bulk_array_native.cpp
    src/recovered/manager_queue_native.cpp
    src/recovered/string_native.cpp
    src/recovered/service48_native.cpp
    src/recovered/context_owner_native.cpp
    src/recovered/context_callback_native.cpp
    src/recovered/clock_native.cpp
    src/recovered/service39_native.cpp
    src/recovered/producer_native.cpp
    src/recovered/array98_native.cpp
    src/recovered/rng_native.cpp
    src/recovered/current_tables_native.cpp)
target_compile_features(dokodemo-core PUBLIC cxx_std_17)
target_include_directories(dokodemo-core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
set_target_properties(dokodemo-core PROPERTIES CXX_EXTENSIONS OFF)
if(MSVC)
    set_target_properties(dokodemo-core PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(dokodemo-core PRIVATE /W4 /EHsc /permissive- /Zc:__cplusplus /utf-8 /Brepro)
else()
    target_compile_options(dokodemo-core PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()
set(DOKODEMO_MODULE_CONTROLS_SOURCE "" CACHE FILEPATH "Optional focused native module-manager controls source inside the workspace")
if(DOKODEMO_MODULE_CONTROLS_SOURCE)
    cmake_path(ABSOLUTE_PATH DOKODEMO_MODULE_CONTROLS_SOURCE BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE module_controls_source)
    cmake_path(IS_PREFIX CMAKE_CURRENT_SOURCE_DIR "${module_controls_source}" NORMALIZE module_controls_inside)
    if(NOT module_controls_inside OR NOT EXISTS "${module_controls_source}" OR NOT module_controls_source MATCHES "\\.cpp$")
        message(FATAL_ERROR "The focused controls source must be a C++ file inside the workspace.")
    endif()
    add_executable(dokodemo-module-controls EXCLUDE_FROM_ALL "${module_controls_source}")
    if(WIN32)
        target_sources(dokodemo-module-controls PRIVATE src/native_codec_workspace_win32.cpp src/startup_memory_backend_win32.cpp src/sascore_runtime_win32.cpp src/memab_runtime_win32.cpp)
        target_compile_definitions(dokodemo-module-controls PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
        dokodemo_configure_prepared_codec(dokodemo-module-controls)
    endif()
    target_link_libraries(dokodemo-module-controls PRIVATE dokodemo-core)
    set_target_properties(dokodemo-module-controls PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        set_target_properties(dokodemo-module-controls PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(dokodemo-module-controls PRIVATE /W4 /WX /EHsc /permissive- /utf-8 /Brepro)
    else()
        target_compile_options(dokodemo-module-controls PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endif()
option(DOKODEMO_CORE_TESTS "Build headless tests of the portable recovery core" ON)
set(DOKODEMO_BACKEND_CONTROLS_SOURCE "" CACHE FILEPATH "Optional focused Win32 backend ownership controls source inside the workspace")
if(DOKODEMO_BACKEND_CONTROLS_SOURCE)
    if(NOT WIN32 OR NOT MSVC)
        message(FATAL_ERROR "Backend ownership controls require Windows and MSVC.")
    endif()
    cmake_path(ABSOLUTE_PATH DOKODEMO_BACKEND_CONTROLS_SOURCE BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE backend_controls_source)
    cmake_path(IS_PREFIX CMAKE_CURRENT_SOURCE_DIR "${backend_controls_source}" NORMALIZE backend_controls_inside)
    if(NOT backend_controls_inside OR NOT EXISTS "${backend_controls_source}" OR NOT backend_controls_source MATCHES "\\.cpp$")
        message(FATAL_ERROR "Backend controls source must be a C++ file inside the workspace.")
    endif()
    add_executable(dokodemo-backend-ownership-controls EXCLUDE_FROM_ALL "${backend_controls_source}" src/startup_memory_backend_win32.cpp)
    target_compile_features(dokodemo-backend-ownership-controls PRIVATE cxx_std_17)
    target_include_directories(dokodemo-backend-ownership-controls PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_compile_definitions(dokodemo-backend-ownership-controls PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(dokodemo-backend-ownership-controls PROPERTIES CXX_EXTENSIONS OFF MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(dokodemo-backend-ownership-controls PRIVATE /W4 /WX /EHsc /permissive- /utf-8 /Brepro)
endif()
if(DOKODEMO_CORE_TESTS)
    add_executable(dokodemo-core-tests tools/core_tests.cpp)
    target_link_libraries(dokodemo-core-tests PRIVATE dokodemo-core)
    set_target_properties(dokodemo-core-tests PROPERTIES CXX_EXTENSIONS OFF)
    if(MSVC)
        set_target_properties(dokodemo-core-tests PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(dokodemo-core-tests PRIVATE /W4 /EHsc /permissive- /Zc:__cplusplus /utf-8 /Brepro)
    else()
        target_compile_options(dokodemo-core-tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME portable-core-tests COMMAND dokodemo-core-tests "${CMAKE_CURRENT_SOURCE_DIR}/assets/native")
    set_tests_properties(portable-core-tests PROPERTIES TIMEOUT 60)
endif()
if(NOT DOKODEMO_BUILD_WINDOWS_APP)
    return()
endif()
if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The Windows application requires Windows and MSVC 2022. Use -DDOKODEMO_BUILD_WINDOWS_APP=OFF for the portable core only.")
endif()
if(CMAKE_SIZEOF_VOID_P AND NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Select an x64 generator, for example -G \"Visual Studio 17 2022\" -A x64.")
endif()
if(MSVC_VERSION LESS 1930 OR MSVC_VERSION GREATER_EQUAL 1950)
    message(FATAL_ERROR "Use the installed Visual Studio 2022 toolchain.")
endif()

set(DOKODEMO_EXTRA_SOURCES "" CACHE STRING "Additional source files; semicolon-separated, relative to this project")
set(DOKODEMO_EXTRA_LIBRARIES "" CACHE STRING "Additional Windows libraries; semicolon-separated")
option(DOKODEMO_PACKAGE_ASSETS "Copy prepared assets/native beside the executable" ON)
option(DOKODEMO_CONSOLE "Use a console subsystem (only for entry points defining main/wmain)" OFF)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/native_main.cpp")
    message(FATAL_ERROR "src/native_main.cpp has not been implemented yet. No placeholder executable is built.")
endif()
file(GLOB native_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER native_modules EXCLUDE REGEX "(/test_[^/]*|_tests?(_[^/]*)?|_main|/main)\\.cpp$")
list(APPEND native_modules "${CMAKE_CURRENT_SOURCE_DIR}/src/native_main.cpp")
get_target_property(core_modules dokodemo-core SOURCES)
foreach(core_source IN LISTS core_modules)
    list(REMOVE_ITEM native_modules "${CMAKE_CURRENT_SOURCE_DIR}/${core_source}")
endforeach()
foreach(extra IN LISTS DOKODEMO_EXTRA_SOURCES)
    cmake_path(ABSOLUTE_PATH extra BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE extra_absolute)
    cmake_path(IS_PREFIX CMAKE_CURRENT_SOURCE_DIR "${extra_absolute}" NORMALIZE extra_inside)
    if(NOT extra_inside OR NOT EXISTS "${extra_absolute}" OR NOT extra_absolute MATCHES "\\.cpp$")
        message(FATAL_ERROR "Extra C++ source must exist inside the workspace: ${extra}")
    endif()
    list(APPEND native_modules "${extra_absolute}")
endforeach()
list(REMOVE_DUPLICATES native_modules)
list(SORT native_modules)

if(DOKODEMO_CONSOLE)
    add_executable(dokodemo-native ${native_modules})
else()
    add_executable(dokodemo-native WIN32 ${native_modules})
endif()
target_compile_features(dokodemo-native PRIVATE cxx_std_17)
set_target_properties(dokodemo-native PROPERTIES CXX_EXTENSIONS OFF MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
target_compile_definitions(dokodemo-native PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_options(dokodemo-native PRIVATE /W4 /EHsc /permissive- /Zc:__cplusplus /utf-8 /Brepro)
target_include_directories(dokodemo-native PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
dokodemo_configure_prepared_codec(dokodemo-native)
target_link_libraries(dokodemo-native PRIVATE dokodemo-core user32 gdi32 opengl32 shell32 ole32 uuid winmm advapi32 bcrypt ${DOKODEMO_EXTRA_LIBRARIES})
target_link_options(dokodemo-native PRIVATE /MACHINE:X64 /INCREMENTAL:NO /Brepro)
set_target_properties(dokodemo-native PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
foreach(configuration DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(dokodemo-native PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration} "${CMAKE_BINARY_DIR}")
endforeach()
if(DOKODEMO_PACKAGE_ASSETS)
    add_custom_command(TARGET dokodemo-native POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -DPROJECT_ROOT=${CMAKE_CURRENT_SOURCE_DIR} -DOUTPUT_ROOT=$<TARGET_FILE_DIR:dokodemo-native> -P "${CMAKE_CURRENT_SOURCE_DIR}/scripts/package-assets.cmake"
        VERBATIM)
endif()
if(DOKODEMO_PACKAGE_ASSETS)
    add_test(NAME native-self-test COMMAND dokodemo-native --self-test)
else()
    add_test(NAME native-self-test COMMAND dokodemo-native --self-test --assets "${CMAKE_CURRENT_SOURCE_DIR}/assets/native")
endif()
set_tests_properties(native-self-test PROPERTIES WORKING_DIRECTORY "$<TARGET_FILE_DIR:dokodemo-native>" TIMEOUT 60)
