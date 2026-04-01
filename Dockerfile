# 1. 基础环境
FROM ubuntu:24.04

# 2. 避免弹窗
ENV DEBIAN_FRONTEND=noninteractive

# 3. 划定地盘
WORKDIR /app

# 4. 安装武器库 (C++ 环境和 wget)
RUN apt-get update && apt-get install -y \
    g++ \
    make \
    libmysqlclient-dev \
    libhiredis-dev \
    wget \
    && rm -rf /var/lib/apt/lists/*

# 5. 【核心修改】：放弃外网询问，直接从华为云镜像站秒下 Bazel 6.4.0 稳定版实体！
RUN wget https://mirrors.huaweicloud.com/bazel/6.4.0/bazel-6.4.0-linux-x86_64 -O /usr/local/bin/bazel \
    && chmod +x /usr/local/bin/bazel

# 6. 搬运代码
COPY . /app

RUN echo 'load("@bazel_tools//tools/build_defs/repo:http.bzl", "http_archive")' >> WORKSPACE \
    && echo 'http_archive(name = "rules_cc", urls = ["https://mirror.bazel.build/github.com/bazelbuild/rules_cc/releases/download/0.0.2/rules_cc-0.0.2.tar.gz", "https://ghproxy.net/https://github.com/bazelbuild/rules_cc/releases/download/0.0.2/rules_cc-0.0.2.tar.gz"])' >> WORKSPACE

# 7. 锻造武器 (明确指定只执行编译，绝不运行！)
RUN make build

# 8. 暴露端口
EXPOSE 8080

# 9. 点火命令
CMD ["make", "run"]
