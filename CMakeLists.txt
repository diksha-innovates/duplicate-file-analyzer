cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

project(duplicate_file_analyzer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Debug)
endif()

# Strict warnings apply only to our own targets (see target_compile_options
# below), not to third-party code pulled in via FetchContent.
set(DFA_WARNING_FLAGS -Wall -Wextra -Wpedantic -Werror)

option(ENABLE_ASAN "Build with AddressSanitizer + UndefinedBehaviorSanitizer" OFF)
option(ENABLE_UBSAN "Build with UndefinedBehaviorSanitizer" OFF)
option(ENABLE_TSAN "Build with ThreadSanitizer" OFF)
option(BUILD_TESTS "Build unit tests" ON)

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address)
endif()

if(ENABLE_UBSAN)
  add_compile_options(-fsanitize=undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=undefined)
endif()

if(ENABLE_TSAN)
  add_compile_options(-fsanitize=thread -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=thread)
endif()

find_package(Threads REQUIRED)
find_package(SQLite3 REQUIRED)

add_library(dfa_core STATIC
  src/scanner.cpp
  src/hash.cpp
  src/grouping.cpp
  src/cache.cpp
  src/report.cpp
  src/worker_pool.cpp
  src/glob.cpp
)
target_include_directories(dfa_core PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(dfa_core PUBLIC Threads::Threads SQLite3::SQLite3)
target_compile_options(dfa_core PRIVATE ${DFA_WARNING_FLAGS})
target_compile_definitions(dfa_core PRIVATE DFA_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

if(APPLE)
  # CommonCrypto ships as part of libSystem on macOS; no extra link needed.
  target_compile_definitions(dfa_core PUBLIC DFA_USE_COMMONCRYPTO=1)
else()
  find_package(OpenSSL REQUIRED)
  target_link_libraries(dfa_core PUBLIC OpenSSL::Crypto)
  target_compile_definitions(dfa_core PUBLIC DFA_USE_OPENSSL=1)
endif()

add_executable(duplicate-file-analyzer src/main.cpp)
target_link_libraries(duplicate-file-analyzer PRIVATE dfa_core)
target_compile_options(duplicate-file-analyzer PRIVATE ${DFA_WARNING_FLAGS})

if(BUILD_TESTS)
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/releases/download/v1.15.2/googletest-1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_subdirectory(tests)
endif()
