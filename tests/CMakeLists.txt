add_executable(test-runner
    unit/test_runner.c
    unit/test_config.c
)

target_include_directories(test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/libappliances/src
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_link_libraries(test-runner PRIVATE appliances)

add_test(NAME unit-tests COMMAND test-runner)
