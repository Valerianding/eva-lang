cmake_minimum_required(VERSION 3.16)
project(eva)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
set(CMAKE_CXX_STANDARD 17)

list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew/Cellar/llvm@12/12.0.1_1")
find_package(LLVM 12 REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")


include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
llvm_map_components_to_libnames(llvm_libs support core irreader)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
add_executable(eva eva-llvm.cpp
        src/EvaLLVM.h
        src/Enviroment.h
        src/Logger.h)
target_link_libraries(eva ${llvm_libs})

