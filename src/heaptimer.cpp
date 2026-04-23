#include "heaptimer.h"

// ================================================================
// swapNode_：交换堆中 i 和 j 两个节点
// 交换完同时更新 ref_，保证 ref_ 里的下标还是对的
// ================================================================
void HeapTimer::swapNode_(size_t i, size_t j) {
    assert(i < heap_.size() && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    ref_[heap_[i].id] = i;  // 更新 i 位置节点的下标记录
    ref_[heap_[j].id] = j;  // 更新 j 位置节点的下标记录
}

// ================================================================
// siftUp_：节点上浮
// 如果当前节点比父节点过期时间早，就和父节点交换，一直到根或者不需要换为止
// ================================================================
void HeapTimer::siftUp_(size_t i) {
    assert(i < heap_.size());
    while (i > 0) {
        size_t parent = (i - 1) / 2;  // 父节点下标
        if (heap_[i] < heap_[parent]) {
            // 当前节点比父节点"小"（过期更早），往上换
            swapNode_(i, parent);
            i = parent;
        } else {
            break;  // 已经比父节点大，位置正确，停止
        }
    }
}

// ================================================================
// siftDown_：节点下沉
// 如果当前节点比子节点过期时间晚，就和最小的子节点交换，一直到叶子或不需要换为止
// 返回 true 表示发生了下沉，false 表示没有移动
// ================================================================
bool HeapTimer::siftDown_(size_t i) {
    assert(i < heap_.size());
    size_t n    = heap_.size();
    size_t orig = i;  // 记录初始位置，用于判断是否发生了移动

    while (true) {
        size_t left  = 2 * i + 1;  // 左子节点下标
        size_t right = 2 * i + 2;  // 右子节点下标
        size_t smallest = i;        // 假设当前节点最小

        // 找左右子节点中更小的那个
        if (left < n && heap_[left] < heap_[smallest]) {
            smallest = left;
        }
        if (right < n && heap_[right] < heap_[smallest]) {
            smallest = right;
        }

        if (smallest == i) break;  // 当前节点已经最小，位置正确，停止

        swapNode_(i, smallest);
        i = smallest;
    }

    return i > orig;  // 发生了下沉返回 true
}

// ================================================================
// del_：删除堆中第 i 个节点
// 做法：把它和末尾节点交换，删掉末尾，再对换上来的节点重新调整位置
// ================================================================
void HeapTimer::del_(size_t i) {
    assert(!heap_.empty() && i < heap_.size());

    size_t last = heap_.size() - 1;

    int removedId = heap_[i].id;  // 先记住要删除的 id

    if (i < last) {
        // 不是末尾节点，先换到末尾
        swapNode_(i, last);
        heap_.pop_back();
        ref_.erase(removedId);  // 从 ref_ 里删掉

        // 换上来的节点重新调整：先尝试下沉，下沉不了再上浮
        if (!siftDown_(i)) {
            siftUp_(i);
        }
    } else {
        // 就是末尾节点，直接删
        heap_.pop_back();
        ref_.erase(removedId);
    }
}

// ================================================================
// add：添加一个连接的超时计时
// 如果 id 已存在就更新超时时间，不存在就新建节点
// ================================================================
void HeapTimer::add(int id, int timeoutMs, const std::function<void()>& cb) {
    assert(id >= 0);

    if (ref_.count(id)) {
        // 已存在：更新过期时间和回调，重新调整堆
        size_t i         = ref_[id];
        heap_[i].expires = Clock::now() + MS(timeoutMs);
        heap_[i].cb      = cb;
        if (!siftDown_(i)) {           // 下沉不了了再上浮
            siftUp_(i);
        }
    } else {
        // 不存在：新建节点，插入堆尾，上浮到正确位置
        size_t i = heap_.size();
        ref_[id] = i;
        heap_.push_back({id, Clock::now() + MS(timeoutMs), cb});
        siftUp_(i);
    }
}

// ================================================================
// update：刷新连接的超时时间
// 连接有新数据来了，把它的过期时间往后推
// ================================================================
void HeapTimer::update(int id, int timeoutMs) {
    assert(!heap_.empty() && ref_.count(id));
    size_t i         = ref_[id];
    heap_[i].expires = Clock::now() + MS(timeoutMs);
    // 时间往后推，节点变"大"，需要下沉
    if (!siftDown_(i)) {
        siftUp_(i);
    }
}

// ================================================================
// tick：检查堆顶，把所有已超时的连接触发回调并删除
// ================================================================
void HeapTimer::tick() {
    if (heap_.empty()) return;

    while (!heap_.empty()) {
        TimerNode& node = heap_.front();  // 堆顶，最快过期的那个

        // 还没到期，停止
        if (std::chrono::duration_cast<MS>(node.expires - Clock::now()).count() > 0) {
            break;
        }

        // 已过期：执行回调，删掉这个节点
        node.cb();
        pop();
    }
}

// ================================================================
// pop：删除堆顶节点
// ================================================================
void HeapTimer::pop() {
    assert(!heap_.empty());
    del_(0);
}

// ================================================================
// clear：清空所有节点
// ================================================================
void HeapTimer::clear() {
    heap_.clear();
    ref_.clear();
}

// ================================================================
// getNextTick：返回距离下一个超时还有多少毫秒
// 传给 epoll_wait 用，让它到时间就醒来执行 tick()
// 堆空返回 -1，epoll_wait 无限等待
// ================================================================
int HeapTimer::getNextTick() {
    if (heap_.empty()) return -1;

    // 计算堆顶距离现在还有多少毫秒
    auto ms = std::chrono::duration_cast<MS>(heap_.front().expires - Clock::now()).count();

    // 已经过期返回 0，让 epoll_wait 立刻返回执行 tick()
    return static_cast<int>(std::max(ms, 0L));
}