cmake_minimum_required(VERSION 3.16)
project(miniptx LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat every warning as something to fix (PRD: no warnings with -Wall -Wextra).
add_compile_options(-Wall -Wextra -Wpedantic)

# Optional AddressSanitizer build: cmake -DMINIPTX_ASAN=ON ..
option(MINIPTX_ASAN "Build with AddressSanitizer" OFF)
if(MINIPTX_ASAN)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

# All compiler stages live in one library so the CLI and the tests share them.
# Each new stage (lexer.cpp, parser.cpp, ...) gets added to this list.
add_library(miniptx_core
  src/lexer.cpp
  src/ast.cpp
  src/parser.cpp
)
target_include_directories(miniptx_core PUBLIC src)

# The command-line compiler: miniptx <file.tiny>
add_executable(miniptx src/main.cpp)
target_link_libraries(miniptx PRIVATE miniptx_core)

# Unit tests: one executable, run through CTest.
enable_testing()
add_executable(unit_tests
  tests/test_main.cpp
  tests/test_lexer.cpp
  tests/test_parser.cpp
)
target_link_libraries(unit_tests PRIVATE miniptx_core)
add_test(NAME unit_tests COMMAND unit_tests)
