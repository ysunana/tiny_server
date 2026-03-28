cc_library(
    name = "mysql_pool_lib",
    srcs = ["sql_connection_pool.cpp"],
    hdrs = ["sql_connection_pool.h"],
    visibility = ["//visibility:public"],
    linkopts = ["-lmysqlclient"], # 极度重要：链接 MySQL 官方库
    deps = [
        "//src/log:log_lib",  # 【新增这一行：给 MySQL 模块发通行证】
    ],
)