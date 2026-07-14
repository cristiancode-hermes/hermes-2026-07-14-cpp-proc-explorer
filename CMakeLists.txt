cmake_minimum_required(VERSION 3.28)
project(proc-explorer VERSION 1.0.0 LANGUAGES CXX
  DESCRIPTION "/proc filesystem explorer — inspect processes in C++23"
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies (FetchContent) ──
include(FetchContent)

# CLI11 (header-only CLI parser)
FetchContent_Declare(cli11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.5.0
  SYSTEM
)
FetchContent_MakeAvailable(cli11)

# nlohmann_json (header-only JSON)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  SYSTEM
)
FetchContent_MakeAvailable(nlohmann_json)

# spdlog (header-only logging)
FetchContent_Declare(spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG        v1.15.1
  SYSTEM
)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use bundled fmt")
FetchContent_MakeAvailable(spdlog)

# Catch2 (test framework)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v3.8.0
  SYSTEM
)
FetchContent_MakeAvailable(Catch2)

# ── Library target ──
# List explicitly so main.cpp is NOT compiled into the library
set(LIB_SOURCES
  src/proc_explorer.cpp
)
set(LIB_HEADERS
  include/proc_explorer/proc_explorer.hpp
)

add_library(procexplore_lib STATIC ${LIB_SOURCES} ${LIB_HEADERS})
target_include_directories(procexplore_lib PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
target_link_libraries(procexplore_lib PUBLIC
  CLI11::CLI11
  nlohmann_json::nlohmann_json
  spdlog::spdlog
)

# ── Main executable ──
add_executable(proc-explorer src/main.cpp)
target_link_libraries(proc-explorer PRIVATE procexplore_lib)
# CLI11 include path for main.cpp (header-only, needs explicit dir)
target_include_directories(proc-explorer PRIVATE
  ${cli11_SOURCE_DIR}/include
)

# ── Tests ──
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ── Install ──
install(TARGETS proc-explorer DESTINATION bin)
install(TARGETS procexplore_lib DESTINATION lib)
install(DIRECTORY include/proc_explorer DESTINATION include)
