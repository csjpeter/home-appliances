add_library(appliances STATIC
    src/core/logger.c
    src/core/config.c
    src/core/base64.c
    src/infrastructure/gree_client.c
    src/domain/appliance_service.c
)

target_include_directories(appliances PUBLIC src)
target_link_libraries(appliances PUBLIC OpenSSL::Crypto)
