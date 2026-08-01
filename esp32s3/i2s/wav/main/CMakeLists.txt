idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")
# storage 是 spiffs 分区的名称
# spiffs_create_partition_image(storage ${CMAKE_SOURCE_DIR}/spiffs FLASH_IN_PROJECT)