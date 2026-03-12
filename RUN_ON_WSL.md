# hxrpc 在 Ubuntu WSL 上的运行指南

本指南将一步一步教你如何在 WSL (Ubuntu) 环境下编译运行 hxrpc 项目。

## 目录

- [前置要求](#前置要求)
- [安装系统依赖](#安装系统依赖)
- [安装 CMake](#安装-cmake)
- [安装项目依赖](#安装项目依赖)
- [克隆项目](#克隆项目)
- [编译项目](#编译项目)
- [运行示例程序](#运行示例程序)
- [常见问题](#常见问题)

---

## 前置要求

- WSL 2 (推荐) 或 WSL 1
- Ubuntu 20.04 LTS 或更高版本
- 已安装 Windows Terminal 或其他终端工具（用于访问 WSL）

---

## 安装系统依赖

首先更新系统包列表：

```bash
sudo apt update
```

安装基础开发工具：

```bash
sudo apt install -y build-essential git wget curl
```

---

## 安装 CMake

hxrpc 需要 CMake 3.20 或更高版本。Ubuntu 20.04 默认的 CMake 版本较低，需要手动安装新版。

### 方法一：使用 apt 安装新版 CMake

```bash
sudo apt install -y software-properties-common
sudo wget -O /etc/apt/trusted.gpg.d/cmake.gpg https://apt.kitware.com/keys/kitware-archive-latest.asc
sudo apt-add-repository "deb https://apt.kitware.com/ubuntu/ $(lsb_release -cs) main"
sudo apt update
sudo apt install -y cmake
```

### 验证 CMake 安装

```bash
cmake --version
```

确保版本 >= 3.20

---

## 安装项目依赖

hxrpc 依赖以下库：

| 依赖 | 说明 |
|------|------|
| protobuf | Google Protobuf 序列化库 |
| libprotobuf-dev | Protobuf 开发文件 |
| zookeeper | Apache Zookeeper 客户端库 |
| libzookeeper-mt-dev | Zookeeper 开发文件 |
| muduo | C++ 网络库 |
| glog | Google 日志库 |
| libgoogle-glog-dev | glog 开发文件 |
| libboost-all-dev | Boost 库（部分依赖） |

### 安装所有依赖

```bash
sudo apt install -y \
    protobuf-compiler \
    libprotobuf-dev \
    libzookeeper-mt-dev \
    libgoogle-glog-dev \
    libboost-all-dev \
    libzmq3-dev \
    uuid-dev \
    libsqlite3-dev \
    libjsoncpp-dev
```

### 手动编译安装 muduo

如果 apt 源中的 muduo 版本不兼容，建议手动编译安装：

```bash
# 1. 克隆 muduo 仓库
cd ~
git clone https://github.com/chenshuo/muduo.git
cd muduo

# 2. 创建构建目录
mkdir build && cd build

# 3. 配置 CMake（编译为静态库，不使用测试）
cmake .. -DMUDUO_BUILD_EXAMPLES=OFF -DMUDUO_BUILD_TESTS=OFF

# 4. 编译并安装
make -j$(nproc)
sudo make install

# 5. 更新库缓存
sudo ldconfig
```

---

## 克隆项目

如果还没有克隆项目：

```bash
cd /mnt/c/Users/hxaxd/project/  # 或者你存放项目的目录
# 或者直接在 WSL home 目录克隆
cd ~
git clone https://github.com/your-repo/hxrpc.git
cd hxrpc
```

---

## 编译项目

### 创建构建目录

```bash
cd hxrpc
mkdir -p build && cd build
```

### 配置 CMake

```bash
cmake ..
```

如果遇到找不到库的问题，可能需要指定库路径：

```bash
cmake .. -DCMAKE_PREFIX_PATH=/usr/local
```

### 编译

```bash
make -j$(nproc)
```

编译完成后，可执行文件会在 `bin` 目录下：

```bash
ls -la ../bin/
```

你应该能看到：

- `server` - RPC 服务端示例
- `client` - RPC 客户端示例

---

## 运行示例程序

### 1. 启动 Zookeeper 服务

hxrpc 依赖 Zookeeper 进行服务发现。你需要先启动 Zookeeper：

```bash
# 安装 Zookeeper（如果还没有）
sudo apt install -y zookeeper

# 启动 Zookeeper
sudo service zookeeper start

# 或者手动启动
# /usr/share/zookeeper/bin/zkServer.sh start
```

验证 Zookeeper 是否运行：

```bash
# 使用 zkCli 连接测试
echo "ruok" | nc localhost 2181
# 应该返回 "imok"
```

### 2. 配置测试文件

项目根目录已有测试配置文件 `bin/test.conf`，内容类似：

```properties
#  RPC 服务器绑定的 IP
rpcserverip=127.0.0.1
#  RPC 服务器绑定的端口
rpcserverport=8000

#  zookeeper 的 IP
zookeeperip=127.0.0.1
#  zookeeper 的端口
zookeeperport=2181
```

### 3. 启动 RPC 服务端

```bash
cd hxrpc/bin
./server ../bin/test.conf
```

### 4. 启动 RPC 客户端

打开另一个终端：

```bash
cd hxrpc/bin
./client ../bin/test.conf
```

如果一切正常，客户端应该能成功调用服务端的服务。

---

## 常见问题

### 1. 找不到 libprotobuf.so 或 libmuduo.so

确保已正确安装并更新了库缓存：

```bash
sudo ldconfig
```

如果仍然找不到，添加库路径：

```bash
export LD_LIBRARY_PATH=/usr/local/lib:$LD_LIBRARY_PATH
```

可以把这行添加到 `~/.bashrc` 持久化。

### 2. CMake 找不到 Protobuf

确保已安装 `libprotobuf-dev`：

```bash
dpkg -L libprotobuf-dev | grep cmake
```

### 3. 编译错误：undefined reference to xxx

检查是否所有依赖库都已正确安装。尝试重新安装所有依赖：

```bash
sudo apt install --reinstall libprotobuf-dev libgoogle-glog-dev libzookeeper-mt-dev
```

### 4. Zookeeper 连接失败

确保 Zookeeper 已启动：

```bash
sudo service zookeeper status
sudo service zookeeper start
```

### 5. 权限问题

如果遇到权限问题：

```bash
sudo chown -R $USER:$USER ~/hxrpc
```

---

## 总结

运行 hxrpc 的完整步骤：

1. 更新系统：`sudo apt update`
2. 安装基础工具：`sudo apt install build-essential git wget curl`
3. 安装 CMake（3.20+）
4. 安装依赖：`sudo apt install protobuf-compiler libprotobuf-dev libzookeeper-mt-dev libgoogle-glog-dev libboost-all-dev`
5. 编译安装 muduo（如果需要）
6. 克隆项目：`git clone https://github.com/your-repo/hxrpc.git`
7. 创建 build 目录并编译：`mkdir build && cd build && cmake .. && make`
8. 启动 Zookeeper
9. 运行 `./server` 和 `./client`

祝你玩得开心！🎉
