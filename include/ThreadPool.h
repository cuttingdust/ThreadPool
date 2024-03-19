#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <utility>
#include <memory>

#include <atomic>
#include <mutex>
#include <thread>
#include <future>
#include <functional>
#include <condition_variable>

#include <vector>
#include <unordered_map>
#include <queue>

#include <iostream>

constexpr int TASK_MAX_THRESHHOLD = INT32_MAX;
constexpr int THREAD_MAX_THRESHHOLD = 1024;
constexpr int THREAD_MAX_IDLE_TIME = 60;

/// 线程池支持的模式
enum PoolMode {
    PM_FIXED = 0,     /// << 固定数量的线程
    PM_CACHED,    /// << 线程数量可动态增长
};

/// 线程类型
class Thread {
public:
    using ThreadFunc = std::function<void(int)>;

    explicit Thread(ThreadFunc func) : func_(std::move(func)), threadId_(generateId++) {

    }

    virtual ~Thread() = default;

public:
    void start() {
        std::thread t(func_, threadId_);
        t.detach();
    }

    int getId() const {
        return threadId_;
    }

private:
    ThreadFunc func_ = nullptr;

    static int generateId;

    int threadId_;
};

int Thread::generateId = 0;


/// 线程池类型
class ThreadPool {
public:
    explicit ThreadPool() :
            initThreadSize_(0), taskSize_(0), curThreadSize_(0), idleThreadSize_(0),
            taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD),
            poolMode_(PoolMode::PM_FIXED), isRunning_(false) {

    };

    ~ThreadPool() {
        isRunning_ = false;

        /// 等待线程池里面所有的线程返回  有两种状态: 阻塞 & 正在执行任务中
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        notEmpty_.notify_all();

        exitCond_.wait(lock, [&]() -> bool { return threads_.size() == 0; });
    }

public:
    /// \brief 设置线程池的工作模式
    void setModel(const PoolMode &mode) {
        if (checkRunningState())
            return;

        poolMode_ = mode;
    }

    /// \brief 设置task任务队列上线阈值
    void setTaskQueMaxThreshHold(int threshHold) {
        if (checkRunningState())
            return;

        taskQueMaxThreshHold_ = threshHold;
    }

    /// \brief 设置线程池cache模式下线程阈值
    void setThreadSizeThreadHold(int threshHold) {
        if (checkRunningState())
            return;

        if (PoolMode::PM_CACHED == poolMode_)
            threadSizeThreshHold_ = threshHold;
    }

    /// \brief  给线程池提交任务
    /// \return 线程返回值
    template<typename Func, typename... Args>
    auto submitTask(Func &&func, Args &&... args) -> std::future<decltype(func(args...))> {

        /// 打包任务,放入任务队列
        using RType = decltype(func(args...));


        auto task = std::make_shared<std::packaged_task<RType>>(
                std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

        std::future<RType> result = task->get_future();

        /// 获取锁
        std::unique_lock<std::mutex> lock(taskQueMtx_);
        /// 用户提交任务,最长不能阻塞超过1s,否则判断提交失败,返回
        if (!notFull_.wait_for(lock, std::chrono::seconds(1),
                               [&]() -> bool { return taskQue_.size() < taskQueMaxThreshHold_; })) {
            std::cerr << "task queue is full sunmit task fail." << std::endl;
            auto task = std::make_shared<std::packaged_task<RType>>([](){ return  RType(); });
            return task->get_future();
        }

        taskQue_.emplace([task](){
            (*task)();
        });
        taskSize_++;

        notEmpty_.notify_all();

        /// cache model 需要根据任务数量和空闲线程的数量, 判断是否需要创建新的线程出来
        /// 任务处理比较紧急 小而快的任务
        if (PoolMode::PM_CACHED == poolMode_
            && taskSize_ > idleThreadSize_
            && curThreadSize_ < threadSizeThreshHold_) {

            std::cout << ">>> create new threadID " << std::endl;

            /// 创建新的线程
            auto thd = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this, std::placeholders::_1));
            int threadId = thd->getId();
            threads_.emplace(threadId, std::move(thd));

            /// 启动线程
            threads_[threadId]->start();

            /// 修改线程个数
            curThreadSize_++;
            idleThreadSize_++;
        }

        return result;
    }

    /// \brief 开启线程池
    void start(int threadSize = std::thread::hardware_concurrency()) {
        isRunning_ = true;

        /// 记录初始线程个数
        initThreadSize_ = threadSize;
        curThreadSize_ = threadSize;

        /// 创建线程对象
        for (int i = 0; i < initThreadSize_; ++i) {

            auto thd = std::make_unique<Thread>([this](auto &&PH1) { threadFunc(std::forward<decltype(PH1)>(PH1)); });
            int threadId = thd->getId();
            threads_.emplace(threadId, std::move(thd));
        }

        /// 启动所有线程
        for (int i = 0; i < initThreadSize_; ++i) {
            threads_[i]->start();
            idleThreadSize_++;
        }
    }

public:
    /// \brief 线程函数
    void threadFunc(int threadId) {

        auto lastTime = std::chrono::high_resolution_clock::now();

        for (;;) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(taskQueMtx_);

                std::cout << "tid: " << std::this_thread::get_id()
                          << " 尝试获取任务" << std::endl;

                /// cache模式下 有可能已经创建了很多的线程, 但是空闲时间超过了60s 应该把多余的线程
                /// 结束回收掉???(超过initThreadSize_数量的线程要进行回收)
                /// 当前时间 - 上一次线程执行的时间 > 60s

                while (taskSize_ == 0) {

                    /// 线程池要结束 回收线程资源
                    if (!isRunning_) {
                        threads_.erase(threadId);
                        std::cout << "threadID: " << std::this_thread::get_id() << " exit!" << std::endl;
                        exitCond_.notify_all();
                        return;
                    }

                    if (PoolMode::PM_CACHED == poolMode_) {
                        /// 每一秒中返回一次 怎么区分: 超时返回? 还是有任务待执行

                        if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1))) {
                            auto now = std::chrono::high_resolution_clock::now();
                            auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
                            if (dur.count() >= THREAD_MAX_IDLE_TIME
                                && curThreadSize_ > initThreadSize_) {
                                /// 开始回收当前线程
                                /// 记录当前线程数量的相关的值修改
                                /// 把线程对象从线程列表容器中删除

                                threads_.erase(threadId);
                                curThreadSize_--;
                                idleThreadSize_--;

                                std::cout << "threadID: " << std::this_thread::get_id() << " exit!" << std::endl;
                                return;
                            }
                        }
                    } else {
                        /// 等待 notEmpty_ 条件
                        notEmpty_.wait(lock);
                    }
                }
                idleThreadSize_--;

                std::cout << "tid " << std::this_thread::get_id()
                          << " 获取任务成功" << std::endl;

                task = taskQue_.front();
                taskQue_.pop();
                taskSize_--;

                if (!taskQue_.empty()) {
                    notEmpty_.notify_all();
                }

                /// 取出任务 进行通知
                notFull_.notify_all();
            }

            if (task != nullptr) {
                task();
            }

            lastTime = std::chrono::high_resolution_clock::now(); /// 更新时间
            idleThreadSize_++;
        }
    }

    bool checkRunningState() const {
        return isRunning_;
    }

public:
    /// 限制拷贝使用
    ThreadPool(const ThreadPool &other) = delete;

    ThreadPool &operator=(const ThreadPool &other) = delete;

private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;  ///线程列表
    size_t initThreadSize_;                         /// 初始的线程数量
    int threadSizeThreshHold_;                      /// 线程数量上限阈值
    std::atomic_int curThreadSize_;                /// 记录当前线程池里面线程的总数量
    std::atomic_int idleThreadSize_;                /// 记录线程的数量

    using Task = std::function<void()>;
    std::queue<Task> taskQue_{};     /// 任务队列
    std::atomic_int taskSize_;                      /// 任务数量
    int taskQueMaxThreshHold_;                      /// 任务队列数量上限阈值

    std::mutex taskQueMtx_;                         /// 保证任务队列的线程安全
    std::condition_variable notFull_;               /// 表示任务队列不满
    std::condition_variable notEmpty_;              /// 表示任务队列不空
    std::condition_variable exitCond_;              /// 等到线程资源全部回收

    PoolMode poolMode_;

    std::atomic_bool isRunning_;                    /// 表示当前线程池的启动状态
};

#endif // THREAD_POOL