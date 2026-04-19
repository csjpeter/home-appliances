add_executable(test-runner
    unit/test_runner.c
    unit/test_config.c
    unit/test_base64.c
    unit/test_device_store.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/infrastructure/gree_client.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/infrastructure/roborock_client.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/base64.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/config.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/logger.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/subnet_scan.c
)

target_include_directories(test-runner PRIVATE
    ${CMAKE_SOURCE_DIR}/libappliances/src
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_link_libraries(test-runner PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_test(NAME unit-tests COMMAND test-runner)

# Roborock functional tests — compile roborock_client.c directly with port
# override so the library's pre-compiled copy (port 54321) is not used.
add_executable(test-roborock-functional
    functional/test_roborock_functional.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/infrastructure/roborock_client.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/logger.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/subnet_scan.c
)

target_include_directories(test-roborock-functional PRIVATE
    ${CMAKE_SOURCE_DIR}/libappliances/src
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_compile_definitions(test-roborock-functional PRIVATE
    ROBOROCK_PORT=15432
)

target_link_libraries(test-roborock-functional PRIVATE OpenSSL::Crypto Threads::Threads)

add_test(NAME roborock-functional-tests COMMAND test-roborock-functional)

# ── Brother functional tests — compile brother_client.c directly with port
# override so the library's pre-compiled copy (port 161) is not used.

add_executable(test-brother-functional
    functional/test_brother_functional.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/infrastructure/brother_client.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/logger.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/subnet_scan.c
)

target_include_directories(test-brother-functional PRIVATE
    ${CMAKE_SOURCE_DIR}/libappliances/src
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_compile_definitions(test-brother-functional PRIVATE
    BROTHER_SNMP_PORT=16100
)

target_link_libraries(test-brother-functional PRIVATE Threads::Threads)

add_test(NAME brother-functional-tests COMMAND test-brother-functional)

# ── Gree functional tests — compile gree_client.c directly with port
# override so the library's pre-compiled copy (port 7000) is not used.
add_executable(test-gree-functional
    functional/test_gree_functional.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/infrastructure/gree_client.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/base64.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/logger.c
)

target_include_directories(test-gree-functional PRIVATE
    ${CMAKE_SOURCE_DIR}/libappliances/src
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_compile_definitions(test-gree-functional PRIVATE
    GREE_PORT=17000
)

target_link_libraries(test-gree-functional PRIVATE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

add_test(NAME gree-functional-tests COMMAND test-gree-functional)

# ── Samsung TV functional tests
add_executable(test-samsung-tv-functional
    functional/test_samsung_tv_functional.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/infrastructure/samsung_tv_client.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/base64.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/logger.c
    ${CMAKE_SOURCE_DIR}/libappliances/src/core/subnet_scan.c
)

target_include_directories(test-samsung-tv-functional PRIVATE
    ${CMAKE_SOURCE_DIR}/libappliances/src
    ${CMAKE_SOURCE_DIR}/tests/common
)

target_compile_definitions(test-samsung-tv-functional PRIVATE
    SAMSUNG_TV_PORT=15500
)

target_link_libraries(test-samsung-tv-functional PRIVATE Threads::Threads)

add_test(NAME samsung-tv-functional-tests COMMAND test-samsung-tv-functional)

# ── libpty: PTY test helper (ported from email-cli, no external deps) ────
add_library(pty STATIC
    ${CMAKE_SOURCE_DIR}/tests/libpty/pty_session.c
    ${CMAKE_SOURCE_DIR}/tests/libpty/pty_screen.c
    ${CMAKE_SOURCE_DIR}/tests/libpty/pty_sync.c
)

target_include_directories(pty PUBLIC
    ${CMAKE_SOURCE_DIR}/tests/libpty
)

target_link_libraries(pty PRIVATE util)

# ── PTY CLI tests — test the built binary via a real pseudo-terminal ─────

set(PTY_DEFS HOME_APPLIANCES_BIN="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/home-appliances")

function(add_pty_test name src)
    add_executable(${name} ${src} ${CMAKE_SOURCE_DIR}/tests/common/test_helpers.h)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/common
    )
    target_compile_definitions(${name} PRIVATE ${PTY_DEFS})
    target_link_libraries(${name} PRIVATE pty)
    add_dependencies(${name} home-appliances)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

add_pty_test(test-pty-ac      ${CMAKE_SOURCE_DIR}/tests/pty/test_pty_ac.c)
add_pty_test(test-pty-vacuum  ${CMAKE_SOURCE_DIR}/tests/pty/test_pty_vacuum.c)
add_pty_test(test-pty-tv      ${CMAKE_SOURCE_DIR}/tests/pty/test_pty_tv.c)
add_pty_test(test-pty-printer ${CMAKE_SOURCE_DIR}/tests/pty/test_pty_printer.c)
