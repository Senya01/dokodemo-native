cmake_minimum_required(VERSION 3.21)

project(dokodemo_windows LANGUAGES CXX)

if(NOT WIN32 OR NOT MSVC OR MSVC_VERSION LESS 1930 OR MSVC_VERSION GREATER_EQUAL 1950)
    message(FATAL_ERROR "Use Visual Studio 2022 with its C++ desktop tools.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Select an x64 generator.")
endif()

set(DOKODEMO_USER_TABLES_DIR "" CACHE PATH "Directory containing the two locally generated headers")
foreach(header IN ITEMS audio_catalog.h recovered/ksc_dispatch_addresses.h)
    if(NOT DOKODEMO_USER_TABLES_DIR OR NOT EXISTS "${DOKODEMO_USER_TABLES_DIR}/${header}")
        message(FATAL_ERROR "Missing generated header ${header}. See windows/README.md.")
    endif()
endforeach()

function(configure_cpp target)
    target_compile_features(${target} PUBLIC cxx_std_17)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_compile_options(${target} PRIVATE /W4 /EHsc /permissive- /Zc:__cplusplus /utf-8 /Brepro)
endfunction()

set(core_manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/core-sources.txt")
set(windows_manifest "${CMAKE_CURRENT_SOURCE_DIR}/src/windows-sources.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${core_manifest}" "${windows_manifest}")
file(STRINGS "${core_manifest}" core_sources)
file(STRINGS "${windows_manifest}" windows_sources)

add_library(dokodemo-core STATIC
    ${core_sources}
    src/platform/retail_cipher_win32.cpp
    src/gameplay/retail_slot_lock_win32.cpp)
configure_cpp(dokodemo-core)
target_include_directories(dokodemo-core PUBLIC src "${DOKODEMO_USER_TABLES_DIR}")
target_link_libraries(dokodemo-core PRIVATE bcrypt)

add_library(dokodemo-win32 STATIC ${windows_sources})
configure_cpp(dokodemo-win32)
target_include_directories(dokodemo-win32 PRIVATE src/platform/audio_ffmpeg_headers)
target_compile_definitions(dokodemo-win32 PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(dokodemo-win32 PUBLIC dokodemo-core PRIVATE bcrypt xinput9_1_0 gdi32 winmm ws2_32 iphlpapi)

add_executable(dokodemo-game WIN32 src/game/main_win32.cpp)
configure_cpp(dokodemo-game)
target_compile_definitions(dokodemo-game PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(dokodemo-game PRIVATE dokodemo-win32 user32 gdi32 shell32)
target_link_options(dokodemo-game PRIVATE /INCREMENTAL:NO /Brepro)
set_target_properties(dokodemo-game PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/game")
foreach(configuration IN ITEMS DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(dokodemo-game PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${configuration} "${CMAKE_BINARY_DIR}/game")
endforeach()

include(CTest)
if(BUILD_TESTING)
    foreach(test IN ITEMS ksc_save_graph retail_savedata_envelope room_input_gate room_word_reading radial_wipe profile_date)
        add_executable(${test}-tests "tools/${test}_tests.cpp")
        configure_cpp(${test}-tests)
        target_link_libraries(${test}-tests PRIVATE dokodemo-core)
        add_test(NAME ${test} COMMAND ${test}-tests)
        set_tests_properties(${test} PROPERTIES ENVIRONMENT "DOKODEMO_TEST_AUDIO=null")
    endforeach()
endif()
