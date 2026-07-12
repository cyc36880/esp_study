idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    EMBED_TXTFILES # 文本文件
                        "index.html"
                    EMBED_FILES   # 二进制文件
                        "man.jpeg"
                        "index.html.gz"
        )
