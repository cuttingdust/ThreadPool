#include "ThreadPool.h"

#include <thread>
#include <iostream>

constexpr int TASK_MAX_THRESHHOLD = 4;

ThreadPool::ThreadPool()
        : initThreadSize_(0), taskSize_(0), taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD), poolMode_(PoolMode::PM_FIXED) {

}

ThreadPool::~ThreadPool() = default;

void ThreadPool::start(int threadSize) {
    /// 记录初始线程个数
    initThreadSize_ = threadSize;

    /// 创建线程对象
    for (int i = 0; i < initThreadSize_; ++i) {

        auto thd = std::make_unique<Thread>(std::bind(&ThreadPool::threadFunc, this));
        threads_.emplace_back(std::move(thd));
    }

    /// 启动所有线程
    for (int i = 0; i < initThreadSize_; ++i) {
        threads_[i]->start();
    }
}

void ThreadPool::setModel(const PoolMode &mode) {
    poolMode_ = mode;
}

void ThreadPool::setTaskQueMaxThreshHold(int threshHold) {
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

    return Result(sp);
}

void ThreadPool::threadFunc() {
//    std::cout << "begin threadFunc tid: " << std::this_thread::get_id() << std::endl;
//    std::cout << "end threadFunc tid 444444: " << std::this_thread::get_id() << std::endl;

    for (;;) {
        std::shared_ptr<Task> task;
        {
            std::unique_lock<std::mutex> lock(taskQueMtx_);

            std::cout << "tid: " << std::this_thread::get_id()
                      << " 尝试获取任务" << std::endl;

            notEmpty_.wait(lock, [&]() -> bool { return taskQue_.size() > 0; });

            std::cout << "tid " << std::this_thread::get_id()
                      << " 获取任务成功" << std::endl;

            task = taskQue_.front();
            taskQue_.pop();
            taskSize_--;

            if (taskQue_.size() > 0) {
                notEmpty_.notify_all();
            }

            /// 取出任务 进行通知
            notFull_.notify_all();
        }

        if (task != nullptr) {
            task->exec();
        }
    }
}

////////////////////线程方法实现/////////////////////////
void Thread::start() {
    std::thread t(func_);
    t.detach();
}

Thread::Thread(Thread::ThreadFunc func)
        : func_(func) {


}


Thread::~Thread() = default;

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
    this->any_ = std::move(any);
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
