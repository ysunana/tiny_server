cc_binary(
    name = "server_app",
    srcs = ["main.cpp"],
    deps = [
        "//src/threadpool:threadpool_lib",
        "//src/http:http_lib", # 新增依赖
        "//src/timer:timer_lib", # 【新增这一行】
        "//src/mysql:mysql_pool_lib",
        "//src/log:log_lib",
    ],
    # 告诉 Bazel 运行前把上级目录的 resources 文件夹拷贝进沙盒
    data = ["//:resources"],
)