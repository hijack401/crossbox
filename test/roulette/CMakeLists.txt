add_executable(RouletteGameTest
  RouletteGameTest.cpp
  ../../src/util/RouletteGame.cpp
)

target_include_directories(RouletteGameTest PRIVATE ../../src)
target_link_libraries(RouletteGameTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(RouletteGameTest)
