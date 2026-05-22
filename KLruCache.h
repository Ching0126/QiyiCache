#pragma once //确保头文件只被编译一次

#include <cstring>//提供memset函数
#include <list>//双向链表
#include <memory>//智能指针
#include <mutex>//互斥锁
#include <unordered_map>//哈希表

#include "KICachePolicy.h"

namespace KamaCache
{
// 前向声明
template<typename Key, typename Value> class KLruCache;

template<typename Key, typename Value>
class LruNode 
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;  // 访问次数
    std::weak_ptr<LruNode<Key, Value>> prev_;  // 改为weak_ptr打破循环引用；
    // 弱引用指针，观察一个对象，但不增加引用计数，不能直接使用对象。

    std::shared_ptr<LruNode<Key, Value>> next_;//共享指针，多个 `shared_ptr` 可以指向同一个对象，内部引用计数管理内存。


public:
    LruNode(Key key, Value value)//构造函数
        : key_(key)
        , value_(value)
        , accessCount_(1) 
    {}

    // 提供必要的访问器
    Key getKey() const { return key_; }//const 成员函数，保证不会修改成员变量，可以被const对象调用;如果不加const，const 对象无法调用
    Value getValue() const { return value_; }
    void setValue(const Value& value) { value_ = value; }//不会修改参数
    size_t getAccessCount() const { return accessCount_; }
    void incrementAccessCount() { ++accessCount_; }

    friend class KLruCache<Key, Value>; //KLruCache可以访问本类的私有成员
};


template<typename Key, typename Value>
class KLruCache : public KICachePolicy<Key, Value>//继承KICachePolicy接口
{
public:
    //C++11引入的类型别名，简化类型定义，增强代码可读性·
    //using 新名字 = 原有类型;
    using LruNodeType = LruNode<Key, Value>;
    using NodePtr = std::shared_ptr<LruNodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    KLruCache(int capacity)
        : capacity_(capacity)
    {
        initializeList();
    }

    ~KLruCache() override = default;//使用默认析构函数，编译器会自动生成一个合适的析构函数来销毁对象，释放资源。

    // 添加缓存
    void put(Key key, Value value) override
    {
        if (capacity_ <= 0)
            return;
        //自动锁管理器,用于自动管理 `std::mutex`（互斥锁）的加锁和解锁。
        //命令执行时会自动调用 `lock()` 方法加锁；函数结束或者返回都会自动调用 `unlock()` 方法解锁，确保线程安全。
        //mutex仅做作用于当前实例的锁，保护当前实例的成员变量不被多个线程同时访问，避免数据竞争和不一致。
        std::lock_guard<std::mutex> lock(mutex_);//自动加锁和解锁，确保线程安全
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            // 如果在当前容器中,则更新value,并调用get方法，代表该数据刚被访问
            updateExistingNode(it->second, value);//更新节点的值，并将其移动到最近访问的位置
            return ;
        }

        addNewNode(key, value);
    }

    bool get(Key key, Value& value) override
    // 获取缓存，返回是否命中，并通过引用参数返回值
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(Key key) override
    {
        Value value{};
        // memset(&value, 0, sizeof(value));   // memset 是按字节设置内存的，对于复杂类型（如 string）使用 memset 可能会破坏对象的内部结构
        get(key, value);
        return value;
    }

    // 删除指定元素
    void remove(Key key) 
    {   
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }

private:
    void initializeList()//初始化函数，创建首尾虚拟节点，并将它们连接起来，形成一个空的双向链表。虚拟节点不存储实际数据，但简化了插入和删除操作的边界条件处理。
    {
        // 创建首尾虚拟节点
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    void updateExistingNode(NodePtr node, const Value& value) //更新节点的值，并将其移动到最近访问的位置
    {
        node->setValue(value);
        moveToMostRecent(node);
    }

    void addNewNode(const Key& key, const Value& value) //添加新节点，
    {
       if (nodeMap_.size() >= capacity_) //如果当前缓存已满，则先驱逐最近最少访问的节点，然后将新节点插入到链表的尾部，并更新哈希表中的映射关系。
       {
           evictLeastRecent();// 驱逐最近最少访问的节点
       }

       NodePtr newNode = std::make_shared<LruNodeType>(key, value);
       insertNode(newNode);
       nodeMap_[key] = newNode;
    }

    // 将该节点移动到最新的位置
    void moveToMostRecent(NodePtr node) 
    {
        removeNode(node);
        insertNode(node);
    }

    void removeNode(NodePtr node) 
    {   //!node->prev_.expired()判断前一个节点是否存在，
        if(!node->prev_.expired() && node->next_) 
        {
            auto prev = node->prev_.lock(); // 将 weak_ptr 转为 shared_ptr（如果没过期）
            prev->next_ = node->next_;
            node->next_->prev_ = prev;
            node->next_ = nullptr; // 清空next_指针，彻底断开节点与链表的连接
        }
    }
 
    // 从尾部插入结点
    void insertNode(NodePtr node) 
    {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        dummyTail_->prev_.lock()->next_ = node; // 使用lock()获取shared_ptr
        dummyTail_->prev_ = node;
    }

    // 驱逐最近最少访问
    void evictLeastRecent() 
    {
        NodePtr leastRecent = dummyHead_->next_;
        removeNode(leastRecent);
        nodeMap_.erase(leastRecent->getKey());
    }

private:
    //NodeMaop是一个哈希表，存储键与节点指针的映射关系，便于快速查找节点
    //NodePtr是一个智能指针，管理节点的生命周期，自动释放内存，避免内存泄漏
    int           capacity_; // 缓存容量
    NodeMap       nodeMap_; // key -> Node 
    std::mutex    mutex_;
    NodePtr       dummyHead_; // 虚拟头结点
    NodePtr       dummyTail_;
};

// LRU优化：Lru-k版本。 通过继承的方式进行再优化
template<typename Key, typename Value>
class KLruKCache : public KLruCache<Key, Value>
{
public:
    KLruKCache(int capacity, int historyCapacity, int k)
        : KLruCache<Key, Value>(capacity) // 调用基类构造
        , historyList_(std::make_unique<KLruCache<Key, size_t>>(historyCapacity))
        , k_(k)
    {}

    Value get(Key key) //父类不是虚类，已经完成了重写，无需再添加override关键字
    {
        // 首先尝试从主缓存获取数据
        Value value{};
        bool inMainCache = KLruCache<Key, Value>::get(key, value);

        // 获取并更新访问历史计数
        size_t historyCount = historyList_->get(key);
        historyCount++;
        historyList_->put(key, historyCount);

        // 如果数据在主缓存中，直接返回
        if (inMainCache) 
        {
            return value;
        }

        // 如果数据不在主缓存，但访问次数达到了k次
        if (historyCount >= k_) 
        {
            // 检查是否有历史值记录
            auto it = historyValueMap_.find(key);
            if (it != historyValueMap_.end()) 
            {
                // 有历史值，将其添加到主缓存
                Value storedValue = it->second;
                
                // 从历史记录移除
                historyList_->remove(key);
                historyValueMap_.erase(it);
                
                // 添加到主缓存
                KLruCache<Key, Value>::put(key, storedValue);
                
                return storedValue;
            }
            // 没有历史值记录，无法添加到缓存，返回默认值
        }

        // 数据不在主缓存且不满足添加条件，返回默认值
        return value;
    }

    void put(Key key, Value value) 
    {
        // 检查是否已在主缓存
        Value existingValue{};
        bool inMainCache = KLruCache<Key, Value>::get(key, existingValue);
        
        if (inMainCache) 
        {
            // 已在主缓存，直接更新
            KLruCache<Key, Value>::put(key, value);
            return;
        }
        
        // 获取并更新访问历史
        size_t historyCount = historyList_->get(key);
        historyCount++;
        historyList_->put(key, historyCount);
        
        // 保存值到历史记录映射，供后续get操作使用
        historyValueMap_[key] = value;
        
        // 检查是否达到k次访问阈值
        if (historyCount >= k_) 
        {
            // 达到阈值，添加到主缓存
            historyList_->remove(key);
            historyValueMap_.erase(key);
            KLruCache<Key, Value>::put(key, value);
        }
    }

private:
    int                                     k_; // 进入缓存队列的评判标准
    std::unique_ptr<KLruCache<Key, size_t>> historyList_; // 访问数据历史记录(value为访问次数)
    std::unordered_map<Key, Value>          historyValueMap_; // 存储未达到k次访问的数据值
};

// lru优化：对lru进行分片，提高高并使用的性能
template<typename Key, typename Value>
class KHashLruCaches
{
public:
    KHashLruCaches(size_t capacity, int sliceNum)
        : capacity_(capacity)
        , sliceNum_(sliceNum > 0 ? sliceNum : std::thread::hardware_concurrency())//__返回当前 CPU 支持的硬件并发线程数__。
    {
        size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_)); // 获取每个分片的大小
        for (int i = 0; i < sliceNum_; ++i)
        {
            lruSliceCaches_.emplace_back(new KLruCache<Key, Value>(sliceSize)); 
        }
    }

    void put(Key key, Value value)
    {
        // 获取key的hash值，并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        lruSliceCaches_[sliceIndex]->put(key, value);
    }

    bool get(Key key, Value& value)
    {
        // 获取key的hash值，并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key)
    {
        Value value;
        memset(&value, 0, sizeof(value));
        get(key, value);
        return value;
    }

private:
    // 将key转换为对应hash值
    size_t Hash(Key key)
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t                                              capacity_;  // 总容量
    int                                                 sliceNum_;  // 切片数量
    std::vector<std::unique_ptr<KLruCache<Key, Value>>> lruSliceCaches_; // 切片LRU缓存
};

} // namespace KamaCache