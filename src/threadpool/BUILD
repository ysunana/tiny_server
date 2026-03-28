cc_library(
    name = "threadpool_lib",
    srcs = ["threadpool.cpp"],
    hdrs = ["threadpool.h"],
    visibility = ["//visibility:public"],
    # 强制告诉链接器这是一个 C++ 项目并需要线程库
    linkopts = ["-lpthread"],
    alwayslink = True, # 这是一个“暴力”选项，强制 Bazel 把符号导出
)