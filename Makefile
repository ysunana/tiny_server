# 1. 编译器与基础编译参数
CXX = g++
# 加入了 -g 参数 这就是让 GDB 能看懂哪一行的终极法宝
CXXFLAGS = -std=c++14 -g -Wall -O0 -pthread -I.

# 2. 需要链接的第三方库
# lmysqlclient: MySQL 驱动
# lhiredis: Redis 驱动
# lcrypto: OpenSSL 密码学底层库 (供咱们手写的 JWT 使用)
LIBS = -lmysqlclient -lhiredis -lcrypto

# 3. 自动扫描所有的源文件 
# (包含当前目录的 main.cpp，以及 src 下面所有子目录的 cpp 文件)
SRCS = $(wildcard *.cpp) $(wildcard src/*.cpp) $(wildcard src/*/*.cpp)

# 4. 生成的可执行文件名
TARGET = server_app

# 默认指令
all: $(TARGET)

# 编译链接规则
$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(TARGET) $(LIBS)

# 清理战场的指令
clean:
	rm -rf $(TARGET)

# 本地一键运行指令
run: $(TARGET)
	./$(TARGET)