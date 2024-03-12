#include "ThreadPool.h"

#include <iostream>
#include <chrono>
#include <thread>

class MyTask : public Task {
public:
    MyTask(int begin, int end)
            : begin_(begin), end_(end) {

    }

    ~MyTask() override = default;

public:
    Any run() override {
        std::cout << "tid: " << std::this_thread::get_id()
                  << " begin!" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(3));
        user_ulong_t sum = 0;
        for (int i = begin_; i < end_; ++i) {
            sum += i;
        }

        std::cout << "tid: " << std::this_thread::get_id()
                  << " end!" << std::endl;

        return {sum};
    }

private:
    int begin_;
    int end_;
};

int main(int argc, char *argv[]) {

    {
        ThreadPool pool;

        pool.setModel(PoolMode::PM_CACHED);
        pool.start(4);

        Result res1 = pool.submitTask(std::make_shared<MyTask>(1, 100000000));
        Result res2 = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        Result res3 = pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));
        pool.submitTask(std::make_shared<MyTask>(200000001, 300000000));

        auto sum1 = res1.get().cast_<user_ulong_t>();
        auto sum2 = res2.get().cast_<user_ulong_t>();
        auto sum3 = res3.get().cast_<user_ulong_t>();

        std::cout << " slave:" << (sum1 + sum2 + sum3) << std::endl;
    }


//    user_ulong_t sum = 0;
//    for (int i = 0; i < 300000000; ++i) {
//        sum += i;
//    }
//
//    std::cout << " master:" << (sum) << std::endl;

    getchar();
}
