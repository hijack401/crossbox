add_executable(SlotsGameTest
  SlotsGameTest.cpp
  ../../src/util/SlotsGame.cpp
)

target_include_directories(SlotsGameTest PRIVATE ../../src)
target_link_libraries(SlotsGameTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SlotsGameTest)
