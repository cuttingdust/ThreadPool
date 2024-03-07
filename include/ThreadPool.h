#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <utility>
#include <vector>
#include <unordered_map>
#include <queue>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

class Any {
public:
    Any() = default;

    ~Any() = default;

    Any(const Any &) = delete;

    Any &operator=(const Any &) = delete;

    Any(Any &&) = default;

    Any &operator=(Any &&) = default;

    /// \brief 这个构造函数可以让Any类型接受任意其他的数据
    template<typename T>
    Any(T data): base_(std::make_unique<Derive < T> > (data)) {

    }

public:
    template<typename T>
    T cast_() {
        Derive <T> *pd = dynamic_cast < Derive <T> * > (base_.get());
        if (nullptr == pd) {
            throw std::runtime_error("Type is unmatch!"); // 抛出标准异常
        }

        return pd->data_;
    }

private:
    class Base {
    public:
        virtual ~Base() = default;
    };

    template<typename T>
    class Derive : public Base {
    public:
        Derive(T data)
                : data_(data) {

        }

    public:
        T data_;
    };

private:
    std::unique_ptr<Base> base_;
};

class Semaphore {
public:
    Semaphore(int limit = 0)
            : resLimit_(limit) {

    }

    ~Semaphore() = default;

public:
    /// \brief 获取一个信号量资源
    void wait() {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [&]() -> bool {
            return resLimit_ > 0;
        });
        resLimit_--;
    }

    /// \brief 增加一个信号量资源
    void post() {
        std::unique_lock<std::mutex> lock(mtx_);
        resLimit_++;
        cond_.notify_all();
    }

private:
    int resLimit_;
    std::mutex mtx_;
    std::condition_variable cond_;
};

class Task;

class Result {
public:
    Result(std::shared_ptr<Task> task, bool isVaild = true);

    ~Result() = default;

public:
    Any get();

    void setVal(Any any);

private:
    Any any_;                           /// 存储任务的返回值
    Semaphore sem_;                     /// 线程通信信号量
    std::shared_ptr<Task> task_;        /// 指向对应获取返回值的任务对象
    std::atomic_bool isVaild_;  /// 是否有效
};

/// 任务抽象基类
class Task {
public:
    Task();

    virtual ~Task() = default;

public:
    virtual Any run() = 0;

    void exec();

    void setResult(Result *res);

private:
    Result *result_;
};

/// 线程池支持的模式
enum PoolMode {
    PM_FIXED = 0,     /// << 固定数量的线程
    PM_CACHED,    /// << 线程数量可动态增长
};

/// 线程类型
class Thread {
public:
    using ThreadFunc = std::function<void(int)>;

    explicit Thread(ThreadFunc func);

    virtual ~Thread();

public:
    void start();

    int getId() const;
private:
    ThreadFunc func_ = nullptr;

    static int generateId;

    int threadId_;
};


/// 线程池类型
class ThreadPool {
public:
    explicit ThreadPool();

    ~ThreadPool();

public:
    /// \brief 设置线程池的工作模式
    void setModel(const PoolMode &mode);

    /// \brief 设置task任务队列上线阈值
    void setTaskQueMaxThreshHold(int threshHold);

    /// \brief 设置线程池cache模式下线程阈值
    void setThreadSizeThreadHold(int threshHold);

    /// \brief  给线程池提交任务
    /// \return 线程返回值
    Result submitTask(const std::shared_ptr<Task> sp);

    /// \brief 开启线程池
    void start(int threadSize = 4);

public:
    /// \brief 线程函数
    void threadFunc(int threadId);

    bool checkRunningState() const;

public:
    /// 限制拷贝使用
    ThreadPool(const ThreadPool &other) = delete;

    ThreadPool &operator=(const ThreadPool &other) = delete;

private:
    std::unordered_map<int, std::unique_ptr<Thread>> threads_;  ///线程列表
    size_t initThreadSize_;                         /// 初始的线程数量
    int threadSizeThreshHold_;                      /// 线程数量上限阈值
    std::atomic_int  curThreadSize_;                /// 记录当前线程池里面线程的总数量
    std::atomic_int idleThreadSize_;                /// 记录线程的数量

    std::queue<std::shared_ptr<Task>> taskQue_;     /// 任务队列
    std::atomic_int taskSize_;                      /// 任务数量
    int taskQueMaxThreshHold_;                      /// 任务队列数量上限阈值

    std::mutex taskQueMtx_;                         /// 保证任务队列的线程安全
    std::condition_variable notFull_;               /// 表示任务队列不满
    std::condition_variable notEmpty_;              /// 表示任务队列不空

    PoolMode poolMode_;

    std::atomic_bool isRunning_;                    /// 表示当前线程池的启动状态
};

#endif // THREAD_POOL