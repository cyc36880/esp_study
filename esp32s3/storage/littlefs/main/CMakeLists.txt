idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS ".")
# storage 是 LittleFS 文件系统分区的名称
littlefs_create_partition_image(storage ../flash_data FLASH_IN_PROJECT)