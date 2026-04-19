cmake_minimum_required(VERSION 3.10)

execute_process(
    COMMAND git describe --tags --always --dirty
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_VERSION)
    set(GIT_VERSION "0.1.0-dev")
endif()

project(home-appliances VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(COMMON_FLAGS "-Wall -Wextra -Werror -pedantic -D_GNU_SOURCE")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_C_FLAGS "${COMMON_FLAGS} -g -fsanitize=address -fno-omit-frame-pointer")
elseif(CMAKE_BUILD_TYPE STREQUAL "Coverage")
    set(CMAKE_C_FLAGS "${COMMON_FLAGS} -g -fprofile-arcs -ftest-coverage")
else()
    set(CMAKE_C_FLAGS "${COMMON_FLAGS} -O2")
endif()

add_compile_definitions(HOME_APPLIANCES_VERSION="${GIT_VERSION}")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

enable_testing()

add_subdirectory(libappliances)

add_executable(home-appliances src/main.c)
target_link_libraries(home-appliances PRIVATE appliances)

add_executable(home-appliances-discovery src/main_discovery.c)
target_link_libraries(home-appliances-discovery PRIVATE appliances)

add_subdirectory(tests)

include(GNUInstallDirs)

install(TARGETS home-appliances home-appliances-discovery
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES docs/man/home-appliances.1
    DESTINATION ${CMAKE_INSTALL_MANDIR}/man1
)

install(FILES home-appliances.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
