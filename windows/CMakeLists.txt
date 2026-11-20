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
    endforeach()

    add_executable(controller-input-tests tools/presentation_input_tests.cpp)
    configure_cpp(controller-input-tests)
    target_link_libraries(controller-input-tests PRIVATE dokodemo-win32)
    add_test(NAME controller-input COMMAND controller-input-tests)

    add_executable(audio-null-queue-tests tools/audio_native_test.cpp)
    configure_cpp(audio-null-queue-tests)
    target_link_libraries(audio-null-queue-tests PRIVATE dokodemo-win32)
    add_test(NAME game-null-pcm COMMAND audio-null-queue-tests --null-queue)

    add_executable(room-meeting-channel-tests tools/room_meeting_channel_tests.cpp)
    configure_cpp(room-meeting-channel-tests)
    target_link_libraries(room-meeting-channel-tests PRIVATE dokodemo-win32)

    set(DOKODEMO_GAME_TEST_MEDIA_DIR "" CACHE PATH "Optional game package containing locally prepared media")
    if(DOKODEMO_GAME_TEST_MEDIA_DIR)
        find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)
        if(NOT EXISTS "${DOKODEMO_GAME_TEST_MEDIA_DIR}/assets/game/post-profile/toro-introduction.bin" OR
           NOT EXISTS "${DOKODEMO_GAME_TEST_MEDIA_DIR}/assets/iso/PSP_GAME/USRDIR/data/DATA.BP")
            message(FATAL_ERROR "Prepare game media with tools/package_game_media.py before enabling local-media tests.")
        endif()

        add_executable(post-profile-tests tools/post_profile_tests.cpp)
        configure_cpp(post-profile-tests)
        target_link_libraries(post-profile-tests PRIVATE dokodemo-win32)
        add_test(NAME game-post-profile COMMAND post-profile-tests "${DOKODEMO_GAME_TEST_MEDIA_DIR}")
        set(DOKODEMO_GAME_DEPARTED_TEST_SLOT "" CACHE PATH "Local completed Toro slot for the new adoption test")
        if(DOKODEMO_GAME_DEPARTED_TEST_SLOT)
            add_test(NAME game-readoption COMMAND "${Python3_EXECUTABLE}" -B
                "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_room_readoption.py"
                "$<TARGET_FILE:post-profile-tests>" "${DOKODEMO_GAME_TEST_MEDIA_DIR}"
                "${DOKODEMO_GAME_DEPARTED_TEST_SLOT}")
        endif()
        add_test(NAME game-robo-introduction COMMAND post-profile-tests "${DOKODEMO_GAME_TEST_MEDIA_DIR}" --robo-introduction 1)
        foreach(actor IN ITEMS usagi inu kaeru)
            add_test(NAME game-${actor}-introduction COMMAND post-profile-tests "${DOKODEMO_GAME_TEST_MEDIA_DIR}" --${actor}-introduction)
        endforeach()

        add_executable(cold-room-actor-tests tools/cold_room_actor_tests.cpp)
        configure_cpp(cold-room-actor-tests)
        target_link_libraries(cold-room-actor-tests PRIVATE dokodemo-core)
        add_test(NAME game-room-actor COMMAND cold-room-actor-tests "${DOKODEMO_GAME_TEST_MEDIA_DIR}")

        add_executable(room-vocabulary-tests tools/room_vocabulary_tests.cpp)
        configure_cpp(room-vocabulary-tests)
        target_link_libraries(room-vocabulary-tests PRIVATE dokodemo-core)
        add_test(NAME game-room-vocabulary COMMAND room-vocabulary-tests "${DOKODEMO_GAME_TEST_MEDIA_DIR}")

        add_executable(room-talk-state-tests tools/room_talk_state_tests.cpp tools/room_topic_flag_tests.cpp
            tools/room_friends_tests.cpp tools/room_friends_exchange_tests.cpp)
        configure_cpp(room-talk-state-tests)
        target_link_libraries(room-talk-state-tests PRIVATE dokodemo-core)
        add_test(NAME game-room-talk-state COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_room_text.py"
            "${DOKODEMO_GAME_TEST_MEDIA_DIR}" "$<TARGET_FILE:room-talk-state-tests>")
        add_test(NAME game-room-talk-roster COMMAND "${Python3_EXECUTABLE}" -B
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_room_text.py"
            "${DOKODEMO_GAME_TEST_MEDIA_DIR}" "$<TARGET_FILE:room-talk-state-tests>" --fallback-all)
    endif()

    get_property(windows_tests DIRECTORY PROPERTY TESTS)
    set_property(TEST ${windows_tests} APPEND PROPERTY ENVIRONMENT "DOKODEMO_TEST_AUDIO=null")
endif()
