cmake_minimum_required(VERSION 3.14)
project(OfflineAssistShell C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fstack-protector-strong")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fstack-protector-strong")

# Build static libraries by default so myshell and ai_worker are self-contained
option(BUILD_SHARED_LIBS "Build shared libraries" OFF)

# Shell executable
add_executable(myshell main.c shell.c)

# AI worker executable
add_executable(ai_worker ai_worker.c sandbox.c seccomp_filter.c)

# --- LLAMA.CPP INTEGRATION ---
# Supports either the git submodule at mini-shell/llama.cpp or -DLLAMA_CPP_DIR=/path/to/llama.cpp
set(LLAMA_CPP_DIR "" CACHE PATH "Optional external path to llama.cpp source tree")

if(LLAMA_CPP_DIR)
    if(EXISTS "${LLAMA_CPP_DIR}/CMakeLists.txt")
        add_subdirectory("${LLAMA_CPP_DIR}" "${CMAKE_BINARY_DIR}/llama.cpp")
    else()
        message(FATAL_ERROR "LLAMA_CPP_DIR='${LLAMA_CPP_DIR}' does not contain CMakeLists.txt")
    endif()
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp/CMakeLists.txt")
    add_subdirectory(llama.cpp)
else()
    message(WARNING "llama.cpp not found at ${CMAKE_CURRENT_SOURCE_DIR}/llama.cpp and -DLLAMA_CPP_DIR not set; building ai_worker in echo-only mode.")
endif()

# Ensure both binaries always land in the same top-level build directory
set_target_properties(myshell ai_worker PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

if(TARGET llama)
    find_package(Threads REQUIRED)
    target_compile_definitions(ai_worker PRIVATE USE_LLAMA)
    target_link_libraries(ai_worker
        PRIVATE
            llama
            Threads::Threads
            m
            dl
    )
    message(STATUS "Building WITH llama.cpp support")
else()
    message(STATUS "Building WITHOUT llama.cpp support (echo mode only)")
endif()

# Optional libcap support
find_library(CAP_LIB cap)
find_path(CAP_INCLUDE_DIR sys/capability.h)
if(CAP_LIB AND CAP_INCLUDE_DIR)
    target_link_libraries(ai_worker PRIVATE ${CAP_LIB})
    target_include_directories(ai_worker PRIVATE ${CAP_INCLUDE_DIR})
    target_compile_definitions(ai_worker PRIVATE HAVE_LIBCAP)
endif()

install(TARGETS myshell ai_worker DESTINATION bin)
