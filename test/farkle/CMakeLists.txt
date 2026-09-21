add_executable(FarkleGameTest
  FarkleGameTest.cpp
  ../../src/util/FarkleGame.cpp
)

target_include_directories(FarkleGameTest PRIVATE ../../src)
target_link_libraries(FarkleGameTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(FarkleGameTest)
