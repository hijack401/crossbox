add_executable(CasinoRefreshPolicyTest CasinoRefreshPolicyTest.cpp)

target_include_directories(CasinoRefreshPolicyTest PRIVATE ../../src)

target_link_libraries(CasinoRefreshPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CasinoRefreshPolicyTest)
