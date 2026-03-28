# 定义伪目标，防止和同名文件冲突
.PHONY: build run clean

# 默认执行的命令（直接敲 make 就会执行这个）
default: run

build:
	@echo "🔨 [1/2] 正在调用 Bazel 编译服务器..."
	bazel build //src:server_app

run: build
	@echo "🚀 [2/2] 编译成功！正在启动 Web Server..."
	@echo "---------------------------------------------------"
	./bazel-bin/src/server_app

clean:
	@echo "🧹 正在清理 Bazel 构建缓存..."
	bazel clean