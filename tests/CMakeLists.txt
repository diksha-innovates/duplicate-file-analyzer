add_executable(dfa_tests
  test_glob.cpp
  test_hash.cpp
  test_scanner.cpp
  test_cache.cpp
  test_worker_pool.cpp
  test_grouping.cpp
  test_no_deletion.cpp
)
target_link_libraries(dfa_tests PRIVATE dfa_core GTest::gtest_main)
target_compile_options(dfa_tests PRIVATE ${DFA_WARNING_FLAGS})
target_compile_definitions(dfa_tests PRIVATE DFA_SOURCE_ROOT="${CMAKE_SOURCE_DIR}")

include(GoogleTest)
gtest_discover_tests(dfa_tests)
