add_executable(PomodoroTimerTest PomodoroTimerTest.cpp)

target_include_directories(PomodoroTimerTest PRIVATE ../../src)

target_link_libraries(PomodoroTimerTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PomodoroTimerTest)
