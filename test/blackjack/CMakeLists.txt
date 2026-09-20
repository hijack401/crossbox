add_executable(BlackjackGameTest
  BlackjackGameTest.cpp
  ../../src/util/BlackjackGame.cpp
)

target_include_directories(BlackjackGameTest PRIVATE ../../src)
target_link_libraries(BlackjackGameTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BlackjackGameTest)
