cmake_minimum_required(VERSION 3.14)
project(scene_render LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

option(SCENE_RENDER_ENABLE_TESTS "Enable tests" ON)

# ----------------------------
# MSVC runtime configuration
# ----------------------------
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
endif()

# ----------------------------
# GoogleTest
# ----------------------------
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/heads/main.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

if(MSVC)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(googletest)



enable_testing()

# ----------------------------
# Core library target
# ----------------------------
add_library(scene_render)

target_include_directories(scene_render
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/api
        ${CMAKE_CURRENT_SOURCE_DIR}/scene
        ${CMAKE_CURRENT_SOURCE_DIR}/render
)

# If you later split math, this becomes an external dependency:
# target_link_libraries(scene_render PUBLIC render_math)

# ----------------------------
# Test helpers
# ----------------------------

function(rs_add_test name)
    add_executable(${name}
        tests/${name}.cpp
    )

    target_compile_definitions(${name}
        PRIVATE SCENE_RENDER_TEST=1
    )

    target_link_libraries(${name}
        PRIVATE
            GTest::gtest
            GTest::gtest_main
            scene_render
    )

    add_test(NAME ${name} COMMAND ${name})
endfunction()


function(rs_add_test_group category name)
    set(target "${category}_${name}")

    add_executable(${target}
        tests/${category}/${name}.cpp
    )

    set_target_properties(${target}
        PROPERTIES FOLDER "tests/${category}"
    )

    target_compile_definitions(${target}
        PRIVATE SCENE_RENDER_TEST=1
    )

    target_link_libraries(${target}
        PRIVATE
            GTest::gtest
            GTest::gtest_main
            scene_render
    )

    add_test(NAME ${target} COMMAND ${target})
endfunction()

# ----------------------------
# Tests
# ----------------------------

# Containers
# rs_add_test_group(containers mpsc_queue)
# rs_add_test_group(containers mpsc_ring_buffer)
# rs_add_test_group(containers spsc_queue)
# rs_add_test_group(containers buffer)
# rs_add_test_group(containers buffer_2)
# rs_add_test_group(containers buffer_spec_test)

# Algorithms
# rs_add_test_group(algo algo_test_next)
# rs_add_test_group(algo algo_test_pipeline)

# Scene
# rs_add_test_group(scene transform_node_test)

# Core systems
# rs_add_test(logging)
# rs_add_test(threading)
# rs_add_test(time)
# rs_add_test(file_0)
# rs_add_test(test_window)
# rs_add_test(hormuz_0)
# rs_add_test(engine)

# Math (will eventually move out of repo cleanly)
# rs_add_test_group(math test_vec3)
# rs_add_test_group(math test_mat4)
# rs_add_test_group(math test_quaternion)
# rs_add_test_group(math test_quaternion_mat4)
# rs_add_test_group(math test_camera)
# rs_add_test_group(math algo_math_spec)