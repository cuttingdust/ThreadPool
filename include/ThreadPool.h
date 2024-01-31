#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>

/// 任务抽象基类
class Task
{
public:
    virtual void run() = 0;
};


/// 线程池支持的模式
enum PoolMode
{
    PM_FIXED,     /// << 固定数量的线程
    PM_CACHED,    /// << 线程数量可动态增长
};


/// 线程类型
class Thread
{
    public:
    private:
};


/// 线程池类型
class ThreadPool
{
public:

private:
    std::vector<Thread*> threads_;                  ///线程列表
    size_t initThreadSize_ = -1;                    /// 初始的线程数量

    std::queue<std::shared_ptr<Task>> taskQue_;     /// 任务队列
    std::atomic_int taskSize_ = 0;                  /// 任务数量
    int taskQueMaxThreadHold_ = 0;                  /// 任务队列数量上限阈值

    std::mutex taskQueMtx_;                         /// 保证任务队列的线程安全
    std::condition_variable notFull_;               /// 表示任务队列不满
    std::condition_variable notEmpty_;              /// 表示任务队列不空
};

#endif // THREAD_POOL