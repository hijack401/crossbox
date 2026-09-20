add_executable(CasinoStoreTest
  CasinoStoreTest.cpp
  ${REPO_ROOT}/src/CasinoStore.cpp
  ${REPO_ROOT}/src/util/BlackjackGame.cpp
)

target_include_directories(CasinoStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(CasinoStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CasinoStoreTest)
