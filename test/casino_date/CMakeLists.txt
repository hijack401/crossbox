add_executable(CasinoDateTest CasinoDateTest.cpp)

target_include_directories(CasinoDateTest PRIVATE ${REPO_ROOT}/src)

target_link_libraries(CasinoDateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CasinoDateTest)
