add_executable(BreathworkSessionTest BreathworkSessionTest.cpp)

target_include_directories(BreathworkSessionTest PRIVATE ../../src)

target_link_libraries(BreathworkSessionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BreathworkSessionTest)
