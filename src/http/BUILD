cc_library(
    name = "http_lib",
    srcs = ["http_conn.cpp"],
    hdrs = ["http_conn.h"],
    visibility = ["//visibility:public"],
    deps = [
        "//src/mysql:mysql_pool_lib", 
        "//src/log:log_lib",
    ],
    linkopts = [
        "-lhiredis", 
    ],
)