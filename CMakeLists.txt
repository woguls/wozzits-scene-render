cmake_minimum_required(VERSION 3.14)
project(scene_render LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(SCENE_RENDER_ENABLE_TESTS "Enable tests" ON)
option(SCENE_RENDER_DEV_BUILD "Use local algo_math" ON)

include(FetchContent)


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

if(SCENE_RENDER_ENABLE_TESTS)
    include(FetchContent)

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
endif()

# ----------------------
# dependency: algo_math
# ----------------------
# 1. If already provided (top-level project)
if(NOT TARGET algo_math::algo_math)

    find_package(algo_math QUIET)

    if(NOT TARGET algo_math::algo_math)
        if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../wozzits-algo-math/CMakeLists.txt")
            add_subdirectory(
                "${CMAKE_CURRENT_LIST_DIR}/../wozzits-algo-math"
                "${CMAKE_BINARY_DIR}/algo_math_build"
            )
        else()
            message(FATAL_ERROR "algo_math not found")
        endif()
    endif()

endif()




# ----------------------------
# Core library target
# ----------------------------


add_library(scene_render)
add_library(scene_render::scene_render ALIAS scene_render)

target_link_libraries(scene_render PUBLIC
     algo_math::algo_math)

add_subdirectory(src)

target_include_directories(scene_render
   PUBLIC
       $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/scene_render>
       $<INSTALL_INTERFACE:include>
)

install(TARGETS scene_render
   EXPORT scene_renderTargets
   ARCHIVE DESTINATION lib
   LIBRARY DESTINATION lib
   RUNTIME DESTINATION bin
)

install(DIRECTORY scene_render/api
   DESTINATION include/scene_render
)

install(EXPORT scene_renderTargets
   FILE scene_renderTargets.cmake
   NAMESPACE scene_render::
   DESTINATION lib/cmake/scene_render
)

include(CMakePackageConfigHelpers)

configure_package_config_file(
    cmake/scene_renderConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/scene_renderConfig.cmake
    INSTALL_DESTINATION lib/cmake/scene_render
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/scene_renderConfig.cmake
    DESTINATION lib/cmake/scene_render
)

# ----------------------------
# Test helpers
# ----------------------------

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
            gtest
            GTest::gtest_main
            algo_math::algo_math
            scene_render
    )

    add_test(NAME ${target} COMMAND ${target})

    get_target_property(GTEST_INCLUDES GTest::gtest INTERFACE_INCLUDE_DIRECTORIES)
message(STATUS "gtest include dirs: ${GTEST_INCLUDES}")
endfunction()

# ----------------------------
# Tests
# ----------------------------

if(SCENE_RENDER_ENABLE_TESTS)
    #rs_add_test_group(transform_node transform_node_1)
    rs_add_test_group(scene_graph scene_graph_test_1)

    rs_add_test_group(render render_ir_test_3)
    rs_add_test_group(render stub_backend_test_4)

    rs_add_test_group(scene_compile scene_compiler_test_2)
endif()
