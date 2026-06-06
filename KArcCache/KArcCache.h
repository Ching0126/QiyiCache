#pragma once

#include "../KICachePolicy.h"
#include "KArcLruPart.h"
#include "KArcLfuPart.h"
#include <memory>

namespace KamaCache 
{

template<typename Key, typename Value>
class KArcCache : public KICachePolicy<Key, Value> 
{
public:
    explicit KArcCache(size_t capacity = 10, size_t transformThreshold = 2)
        : capacity_(capacity)
        , transformThreshold_(transformThreshold)
        , lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
        , lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    ~KArcCache() override = default;

    void put(Key key, Value value) override 
    {
        checkGhostCaches(key);

        // 检查 LFU 部分是否存在该键
        bool inLfu = lfuPart_->contain(key);
        // 更新 LRU 部分缓存
        lruPart_->put(key, value);
        // 如果 LFU 部分存在该键，则更新 LFU 部分
        if (inLfu) 
        {
            lfuPart_->put(key, value);
        }
    }

    bool get(Key key, Value& value) override 
    {
        checkGhostCaches(key);

        bool shouldTransform = false;
        if (lruPart_->get(key, value, shouldTransform)) 
        {
            if (shouldTransform) 
            {
                lfuPart_->put(key, value);
            }
            return true;
        }
        return lfuPart_->get(key, value);
    }

    Value get(Key key) override 
    {
        Value value{};
        get(key, value);
        return value;
    }

private:
    // ARC 幽灵缓存检查 —— 自适应容量调节的核心机制
    // 
    // 背景：
    //   ARC 维护两组缓存：T1(一次命中)/T2(多次命中)，以及对应的幽灵缓存 B1/B2。
    //   幽灵缓存只存 key（不存 value），当淘汰的 key 被再次访问时触发容量调整。
    //
    // 逻辑：
    //   如果 key 命中 B1（LRU 幽灵）→ 说明最近太早淘汰了一次命中页，
    //      应缩小 LFU 容量，扩大 LRU 容量，让 T1 能多保留一些。
    //   如果 key 命中 B2（LFU 幽灵）→ 说明最近太早淘汰了多次命中页，
    //      应缩小 LRU 容量，扩大 LFU 容量，让 T2 能多保留一些。
    //
    //   容量约束始终满足：|T1| + |T2| ≤ 总容量
    bool checkGhostCaches(Key key) 
    {
        bool inGhost = false;
        // 检查 key 是否在 LRU 幽灵缓存 (B1) 中
        if (lruPart_->checkGhost(key)) 
        {
            // 命中 B1：优先缩小 LFU 容量，把省下的配额给 LRU
            if (lfuPart_->decreaseCapacity()) 
            {
                lruPart_->increaseCapacity();
            }
            inGhost = true;
        } 
        // 检查 key 是否在 LFU 幽灵缓存 (B2) 中
        else if (lfuPart_->checkGhost(key)) 
        {
            // 命中 B2：优先缩小 LRU 容量，把省下的配额给 LFU
            if (lruPart_->decreaseCapacity()) 
            {
                lfuPart_->increaseCapacity();
            }
            inGhost = true;
        }
        return inGhost;
    }

private:
    size_t capacity_;
    size_t transformThreshold_;
    std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};

} // namespace KamaCache