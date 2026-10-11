# SPDX-License-Identifier: LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Proprietary; the owner is the author.
# Created: 2026-04-19
# Build rules for the libappliances static library.

add_library(appliances STATIC
    src/core/logger.c
    src/core/config.c
    src/core/base64.c
    src/core/subnet_scan.c
    src/infrastructure/gree_client.c
    src/infrastructure/samsung_tv_client.c
    src/infrastructure/brother_client.c
    src/infrastructure/roborock_client.c
    src/domain/appliance_service.c
)

target_include_directories(appliances PUBLIC src)
target_link_libraries(appliances PUBLIC OpenSSL::Crypto Threads::Threads)
