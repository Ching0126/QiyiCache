#pragma once //确保头文件只被编译一次
//pragma once未出来前，一般用ifndef和define来实现头文件的防止重复包含机制：
//#ifndef KAMACACHE_H   // 如果 KAMACACHE_H 未定义
//#define KAMACACHE_H   // 则定义它，并编译以下内容
// ...
//#endif                // 结束守卫

namespace KamaCache{
    //namespace:防止命名冲突,若其他库也有同名的类或函数，使用时可以明确指定用以区分。
template <typename Key, typename Value>//模板类，Key和Value是占位符，实际使用时会被具体类型替换
class KICachePolicy//指明这是一个类的模板
{
public:
    virtual ~KICachePolicy() {};//析构函数

    // 添加缓存接口
    virtual void put(Key key, Value value) = 0;

    // key是传入参数  访问到的值以传出参数的形式返回 | 访问成功返回true
    virtual bool get(Key key, Value& value) = 0;
    // 如果缓存中能找到key，则直接返回value
    virtual Value get(Key key) = 0;

};
//纯虚函数：=0表示这是一个纯虚函数，必须在派生类中实现，否则派生类也将成为抽象类，无法实例化。
//他的接收函数类型不确定、函数实现也不去确定。
} // namespace KamaCache