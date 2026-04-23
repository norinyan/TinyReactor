#ifndef HEAPTIMER_H
#define HEAPTIMER_H

#include <functional>
#include <chrono>
#include <vector>
#include <unordered_map>
#include <cassert>

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::steady_clock::time_point;
using MS        = std::chrono::milliseconds;

class HeapTimer{
public:
    HeapTimer() = default;
    ~HeapTimer() { clear( ); }

    // 添加一个连接的超时计时
    // id：连接的 fd，timeoutMs：超时毫秒数，cb：超时后执行的回调
    void add(int id, int timeoutMs, const std::function<void()>& cb);

    // 连接有新数据来了，刷新它的超时时间
    void update(int id, int timeoutMs);

    // 检查堆顶，把所有已超时的连接触发回调并清除
    void tick();

    // 删除堆顶节点
    void pop();

    // 清空所有节点
    void clear();

    // 返回距离下一个超时还有多少毫秒，传给 epoll_wait 用
    // 堆空返回 -1，让 epoll_wait 无限等待
    int getNextTick();

private:
    // 堆里存的节点
    struct TimerNode {
        int       id;       // 连接的 fd
        TimePoint expires;  // 过期时间点
        std::function<void()> cb;  // 超时回调

        // 重载 < 运算符：过期时间早的"更小"，用于最小堆排序
        bool operator<(const TimerNode& t) const {
            return expires < t.expires;
        }
    };

    // 交换堆中两个节点，同时更新 ref_ 里的下标记录
    void swapNode_(size_t i, size_t j);

    // 节点上浮：过期时间比父节点早就往上换，直到正确位置
    void siftUp_(size_t i);

    // 节点下沉：过期时间比子节点晚就往下换，直到正确位置
    // 返回是否发生了下沉
    bool siftDown_(size_t i);

    // 删除堆中第 i 个节点
    // 做法：和末尾节点交换，删末尾，再对换上来的节点重新调整位置
    void del_(size_t i);

private:
    std::vector<TimerNode>          heap_;  // 小根堆，数组模拟
    std::unordered_map<int, size_t> ref_;   // fd → 堆中下标，O(1) 定位
};






#endif