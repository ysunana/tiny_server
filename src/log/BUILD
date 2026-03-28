cc_library(
    name = "log_lib",
    srcs = ["log.cpp"],
    hdrs = [
        "log.h", 
        "block_queue.h"
    ],
    visibility = ["//visibility:public"],
    
    # 【极其致命的考点】：链接 pthread 库
    linkopts = ["-pthread"], 
)