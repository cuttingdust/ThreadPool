#include "ThreadPool.h"

#include <thread>
#include <iostream>

constexpr int TASK_MAX_THRESHHOLD = INT32_MAX;
constexpr int THREAD_MAX_THRESHHOLD = 1024;
constexpr int THREAD_MAX_IDLE_TIME = 60;

ThreadPool::ThreadPool()
        : initThreadSize_(0), taskSize_(0), curThreadSize_(0), idleThreadSize_(0),
          taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), threadSizeThreshHold_(THREAD_MAX_THRESHHOLD),
          poolMode_(PoolMode::PM_FIXED), isRunning_(false) {

}

ThreadPool::~ThreadPool() {
    isRunning_ = false;

    /// 等待线程池里面所有的线程返回  有两种状态: 阻塞 & 正在执行任务中
    std::unique_lock<std::mutex> lock(taskQueMtx_);
    notEmpty_.notify_all();

    exitCond_.wait(lock, [&]() -> bool { return threads_.size() == 0; });

}

void ThreadPool::start(int threadSize) {

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

void ThreadPool::setModel(const PoolMode &mode) {
    if (checkRunningState())
        return;

    poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshHold) {
    if (checkRunningState())
        return;

    taskQueMaxThreshHold_ = threshHold;
}

Result ThreadPool::submitTask(const std::shared_ptr<Task> sp) {

    std::unique_lock<std::mutex> lock(taskQueMtx_);

    if (!notFull_.wait_for(lock, std::chrono::seconds(1),
                           [&]() -> bool { return taskQue_.size() < taskQueMaxThreshHold_; })) {
        std::cerr << "task queue is full sunmit task fail." << std::endl;

        return Result(sp, false);
    }

    taskQue_.emplace(sp);
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

    return Result(sp);
}

void ThreadPool::threadFunc(int threadId) {

    auto lastTime = std::chrono::high_resolution_clock::now();

    for (;;) {
        std::shared_ptr<Task> task;
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
            task->exec();
        }

        lastTime = std::chrono::high_resolution_clock::now(); /// 更新时间
        idleThreadSize_++;
    }
}

bool ThreadPool::checkRunningState() const {
    return isRunning_;
}

void ThreadPool::setThreadSizeThreadHold(int threshHold) {
    if (checkRunningState())
        return;

    if (PoolMode::PM_CACHED == poolMode_)
        threadSizeThreshHold_ = threshHold;
}

////////////////////线程方法实现/////////////////////////
int Thread::generateId = 0;

Thread::Thread(Thread::ThreadFunc func)
        : func_(func), threadId_(generateId++) {


}

Thread::~Thread() = default;

void Thread::start() {
    std::thread t(func_, threadId_);
    t.detach();
}

int Thread::getId() const {
    return threadId_;
}

Result::Result(std::shared_ptr<Task> task, bool isVaild)
        : task_(std::move(task)), isVaild_(isVaild) {
    task_->setResult(this);
}

Any Result::get() {
    if (!isVaild_) {
        return {};
    }

    sem_.wait();
    return std::move(any_);
}

void Result::setVal(Any any) {
    any_ = std::move(any);
    sem_.post();
}

Task::Task() : result_(nullptr) {

}

void Task::exec() {
    if (result_) {
        result_->setVal(run());
    }
}

void Task::setResult(Result *res) {
    result_ = res;
}
