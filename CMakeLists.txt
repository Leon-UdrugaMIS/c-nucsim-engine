cmake_minimum_required(VERSION 3.20)
project(rbwr_unit2 VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(rbwr_core STATIC src/core/engine.cpp)
target_include_directories(rbwr_core PUBLIC include)
target_compile_options(rbwr_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(rbwr_server src/server/main.cpp)
target_link_libraries(rbwr_server PRIVATE rbwr_core)
target_compile_options(rbwr_server PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(rbwr_http src/server/http_main.cpp)
target_link_libraries(rbwr_http PRIVATE rbwr_core)
target_compile_options(rbwr_http PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(rbwr_cli src/cli/main.cpp)
target_link_libraries(rbwr_cli PRIVATE rbwr_core)
target_compile_options(rbwr_cli PRIVATE -Wall -Wextra -Wpedantic -Werror)

include(CTest)
if(BUILD_TESTING)
  add_executable(rbwr_tests tests/smoke_test.cpp)
  target_link_libraries(rbwr_tests PRIVATE rbwr_core)
  target_compile_options(rbwr_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
  add_test(NAME rbwr_smoke COMMAND rbwr_tests)
endif()
