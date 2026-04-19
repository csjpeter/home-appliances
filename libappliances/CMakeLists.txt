add_library(appliances STATIC
    src/core/logger.c
    src/core/config.c
    src/infrastructure/gree_client.c
    src/domain/appliance_service.c
)

target_include_directories(appliances PUBLIC src)
