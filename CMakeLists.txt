cmake_minimum_required(VERSION 3.16)
project(ObscuraGateway VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)


set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    obscuraproto
    GIT_REPOSITORY https://github.com/kretoffer/ObscuraProto.git
    GIT_TAG main
)
FetchContent_MakeAvailable(obscuraproto)


set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

option(BUILD_OBSCURAGATEWAY_TESTS "Build ObscuraGateway tests" ON)


if(BUILD_OBSCURAGATEWAY_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )
    FetchContent_MakeAvailable(googletest)
endif()


add_library(obscuragateway
    src/gateway.cpp
    src/config.cpp
    src/router.cpp
    src/backend_pool.cpp
    src/backend_node.cpp
    src/correlation.cpp
    src/stream_manager.cpp
    src/internal_protocol.cpp
)

target_include_directories(obscuragateway PUBLIC include)
target_link_libraries(obscuragateway PUBLIC obscuraproto yaml-cpp)


add_executable(obscuragatewayd src/main.cpp)
target_link_libraries(obscuragatewayd PRIVATE obscuragateway)


if(BUILD_OBSCURAGATEWAY_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
