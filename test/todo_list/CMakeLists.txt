add_executable(TodoListTest
  TodoListTest.cpp
  ${REPO_ROOT}/src/TodoStore.cpp
)

target_include_directories(TodoListTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(TodoListTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TodoListTest)
