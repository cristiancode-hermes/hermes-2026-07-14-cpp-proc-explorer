cmake_minimum_required(VERSION 3.28)
project(tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Catch2 comes from the parent's FetchContent — do not redeclare

add_executable(test_procexplorer test_main.cpp)
target_include_directories(test_procexplorer PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(test_procexplorer PRIVATE
  Catch2::Catch2WithMain
  procexplore_lib
)

include(CTest)
add_test(NAME test_procexplorer COMMAND test_procexplorer)
