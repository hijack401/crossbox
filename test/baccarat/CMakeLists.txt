add_executable(BaccaratGameTest
  BaccaratGameTest.cpp
  ../../src/util/BaccaratGame.cpp
)

target_include_directories(BaccaratGameTest PRIVATE ../../src)
target_link_libraries(BaccaratGameTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BaccaratGameTest)
