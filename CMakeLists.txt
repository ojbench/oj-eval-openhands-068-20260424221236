
cmake_minimum_required(VERSION 3.10)
project(TLSFAllocator)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add executable
add_executable(code main.cpp allocator.cpp)

# Set compiler flags
target_compile_options(code PRIVATE -Wall -Wextra -O2)
