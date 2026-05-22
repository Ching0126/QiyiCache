# QiyiCache 🚀

高并发场景下的 C++ 多策略缓存淘汰库，支持 **LRU、LFU、ARC、LRU-K、LFU-Aging** 等多种缓存策略，并提供哈希分片高并发优化版本。

> **QiyiCache** 基于 [KamaCache](https://github.com/youngyangyang04/KamaCache) 进行二次开发，在原项目基础上进行了多项增强。

---

## 📖 目录

- [什么是缓存？](#什么是缓存)
- [支持的缓存策略](#支持的缓存策略)
- [架构设计](#架构设计)
- [快速开始](#快速开始)
- [接口说明](#接口说明)
- [性能测试](#性能测试)
- [与 KamaCache 的改进点](#与-kamacache-的改进点)
- [应用场景推荐](#应用场景推荐)

---

## 什么是缓存？

缓存是将高频访问的数据暂存到内存中，是加速数据访问的存储，降低延迟，提高吞吐率的利器。

### 为什么要实现缓存系统？

因缓存的使用相关需求，**通过牺牲一部分服务器内存，减少对磁盘或者数据库资源进行直接读写，可换取更快响应速度**。尤其是处理高并发的场景，负责存储经常访问的数据，通过设计合理的缓存机制提高资源的访问效率。

由于服务器的内存是有限的，我们不能把所有数据都存放在内存中，因此需要一种机制来决定当使用内存超过一定标准时，应该删除哪些数据，这就涉及到**缓存淘汰策略**的选择。

### 实际应用场景

| 系统 | 使用策略 | 说明 |
|------|---------|------|
| **Linux Kernel** | LRU | 页面缓存(Page Cache)管理，物理内存不足时淘汰最近未使用页面 |
| **Android** | LRU | `LRUCache` 类管理有限内存中的应用数据缓存 |
| **Redis** | LRU / LFU | `volatile-lru`、`allkeys-lfu` 等淘汰策略 |
| **PostgreSQL** | ARC | 部分缓存扩展中实现了 ARC 机制 |
| **Memcached** | LRU | 默认使用 LRU 淘汰旧数据 |

---

## 支持的缓存策略

| 策略 | 类名 | 核心思想 | 适用场景 |
|------|------|---------|---------|
| **LRU** | `KLruCache` | 最近最少使用，淘汰最久未访问的数据 | 通用的访问局部性场景 |
| **LRU-K** | `KLruKCache` | 数据被访问 K 次后才进入缓存，防止一次性数据污染 | 区分热点与冷数据 |
| **Hash-LRU** | `KHashLruCaches` | LRU + 哈希分片，将缓存分片到多个 LRU 实例 | 高并发读写 |
| **LFU** | `KLfuCache` | 最不经常使用，淘汰访问频率最低的数据 | 访问频率差异明显的场景 |
| **LFU-Aging** | `KLfuCache` (带`maxAverageNum`参数) | LFU + 访问频率衰减，防止历史高频数据永久占据缓存 | 工作负载动态变化的场景 |
| **Hash-LFU** | `KHashLfuCache` | LFU + 哈希分片 | 高并发 + 频率访问模式 |
| **ARC** | `KArcCache` | 自适应替换缓存，动态平衡 LRU 和 LFU | 工作负载剧烈变化 |

### 各策略详解

#### 📌 LRU（Least Recently Used）
- **KLruCache** - 通过哈希表 + 双向链表实现，每次访问将节点移到链表尾部，淘汰时从链表头部移除
- **KLruKCache** - LRU 的改进版，数据被访问 **K 次** 后才允许进入主缓存，有效过滤一次性访问的数据污染
- **KHashLruCaches** - 通过哈希将 key 分散到多个 LRU 分片上，显著降低锁竞争

#### 📌 LFU（Least Frequently Used）
- **KLfuCache** - 通过「访问频次 → 双向链表」的映射结构实现，每个频次对应一个链表（FreqList），访问时将节点从低频链表迁移到高频链表
- **LFU-Aging** - 在 LFU 基础上增加「平均访问频次」监控，当平均频次超过阈值时对所有节点的频次进行衰减（减半），使历史高频数据有机会被新热点替代

#### 📌 ARC（Adaptive Replacement Cache）
- **KArcCache** - 结合 LRU 和 LFU 的优点，维护两个缓存部分（LRU 和 LFU）和各自的幽灵缓存
- 通过调整两个部分的大小比例自适应不同的访问模式
- 幽灵缓存记录被淘汰的 key，用于感知访问模式变化

---

## 架构设计

```
QiyiCache/
├── KICachePolicy.h          # 缓存策略抽象接口基类
├── KLruCache.h              # LRU 系列：LRU、LRU-K、Hash-LRU
├── KLfuCache.h              # LFU 系列：LFU、LFU-Aging、Hash-LFU
├── KArcCache/               # ARC 自适应缓存
│   ├── KArcCache.h          #   ARC 主类
│   ├── KArcCacheNode.h      #   通用节点类
│   ├── KArcLruPart.h        #   ARC 的 LRU 部分
│   └── KArcLfuPart.h        #   ARC 的 LFU 部分
├── testAllCachePolicy.cpp   # 多场景基准测试
├── CMakeLists.txt           # CMake 构建配置
└── images/hitTest.jpg       # 命中率对比图
```

### 类继承关系

```
KICachePolicy<Key, Value>         (抽象接口：put / get)
    ├── KLruCache<Key, Value>     (LRU 实现)
    │   └── KLruKCache            (LRU-K，继承自 KLruCache)
    ├── KLfuCache<Key, Value>     (LFU 实现)
    └── KArcCache<Key, Value>     (ARC 实现)
```

### 线程安全

所有缓存策略均使用 `std::mutex` + `std::lock_guard` 实现线程安全，支持多线程并发读写。

### 关键设计特性

- **👻 幽灵缓存（Ghost Cache）**：ARC 和 LFU 使用幽灵缓存记录被淘汰的 key，仅保存元信息不保存 value，用于感知访问模式的变化
- **🔄 频率衰减（Frequency Decay）**：LFU-Aging 通过定期衰减防止缓存污染
- **⚡ 哈希分片（Hash Sharding）**：`KHashLruCaches` 和 `KHashLfuCache` 通过哈希将 key 映射到不同分片，减少锁竞争，提升并发性能

---

## 快速开始

### 环境要求

- C++17 及以上
- CMake 3.10 及以上

### 构建与运行

```bash
# 克隆项目
git clone https://github.com/Ching0126/QiyiCache.git
cd QiyiCache

# 构建
mkdir build && cd build
cmake ..
make

# 运行测试
./main
```

### 示例代码

```cpp
#include "KLruCache.h"
#include "KLfuCache.h"
#include "KArcCache/KArcCache.h"

// LRU 缓存
KamaCache::KLruCache<int, std::string> lru(100);
lru.put(1, "data1");
std::string value;
lru.get(1, value);  // true, value = "data1"

// LFU 缓存
KamaCache::KLfuCache<int, std::string> lfu(100);
lfu.put(1, "data1");
lfu.get(1, value);  // 访问 1 次 → 频次+1

// LFU-Aging（maxAverageNum=20000，超过后所有节点频次减半）
KamaCache::KLfuCache<int, std::string> lfuAging(100, 20000);

// ARC 自适应缓存
KamaCache::KArcCache<int, std::string> arc(100);
arc.put(1, "data1");

// LRU-K（K=2，访问2次后才进入主缓存）
KamaCache::KLruKCache<int, std::string> lruk(100, 1000, 2);

// Hash-LRU（分片高并发优化）
KamaCache::KHashLruCaches<int, std::string> hashLru(1000, 4);
hashLru.put(1, "data1");
```

---

## 接口说明

所有缓存策略均继承自 `KICachePolicy<Key, Value>` 抽象基类：

| 接口 | 说明 |
|------|------|
| `void put(Key key, Value value)` | 添加/更新缓存 |
| `bool get(Key key, Value& value)` | 获取缓存，通过传出参数返回 value，命中返回 true |
| `Value get(Key key)` | 获取缓存，命中返回 value，未命中返回默认值 |

额外接口（特定类）：

| 类 | 接口 | 说明 |
|-----|------|------|
| `KLruCache` | `void remove(Key key)` | 删除指定键 |
| `KLfuCache` | `void purge()` | 清空所有缓存 |
| `KHashLfuCache` | `void purge()` | 清空所有分片的缓存 |

---

## 性能测试

测试包含 **3 个场景**，对比 5 种策略（LRU、LFU、ARC、LRU-K、LFU-Aging）的命中率：

### 场景 1：热点数据访问
- 少量热点 key（20个）+ 大量冷 key（5000个）
- 70% 概率访问热点，30% 概率访问冷数据
- **考验策略区分热点 vs 冷数据的能力**

### 场景 2：循环扫描
- 顺序扫描 + 随机跳跃 + 范围外访问
- 60% 顺序扫描，30% 随机跳跃，10% 范围外
- **考验策略应对顺序扫描时的表现**

### 场景 3：工作负载剧烈变化
- 5 个阶段：热点访问 → 大范围随机 → 顺序扫描 → 局部性随机 → 混合访问
- **考验策略的适应性，ARC 和 LFU-Aging 在此场景表现突出**

### 命中率对比

![命中率对比](images/hitTest.jpg)

> 运行 `./build/main` 即可在本地跑出测试结果。

---

## 与 KamaCache 的改进点

相比于原版 [KamaCache](https://github.com/youngyangyang04/KamaCache)，**QiyiCache** 做出了以下改进：

| 改进项 | 说明 |
|--------|------|
| **添加 ARC 缓存策略** | 完整实现 Adaptive Replacement Cache，包含 LRU 部分、LFU 部分和幽灵缓存机制 |
| **完善文档注释** | 代码中添加了大量中文注释，解释设计原理和关键语法 |
| **修复已知问题** | 修复了弱指针循环引用、指针置空等问题 |
| **增强 LFU 实现** | 增加频率衰减（Aging）机制，防止缓存污染 |
| **丰富测试场景** | 增加多阶段负载变化测试，更贴近真实场景 |

---

## 应用场景推荐

| 场景 | 推荐策略 | 理由 |
|------|---------|------|
| 通用场景，访问局部性强 | **LRU** | 实现简单，性能好 |
| 需防止一次性数据污染 | **LRU-K** | K=2 即可过滤大部分冷数据 |
| 热点数据明显且稳定 | **LFU** | 访问频率能准确反映数据热度 |
| 访问模式动态变化 | **ARC / LFU-Aging** | 自适应能力强 |
| 高并发读写 | **Hash-LRU / Hash-LFU** | 分片减少锁竞争 |
| 数据库缓存、页面缓存 | **ARC** | PostgreSQL 等数据库使用 |

---

## 许可证

本项目基于 [GNU General Public License v3.0](LICENSE) 开源。

---

**QiyiCache** — 为高并发场景打造的多策略 C++ 缓存库。