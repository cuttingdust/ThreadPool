#include "ThreadPool.h"

using uLong = unsigned long long;

int sum1(int a, int b) {
    return a + b;
}

int sum2(int a, int b, int c) {
    return a + b + c;
}

int main(int argc, char *argv[]) {

    ThreadPool pool;
    pool.start(4);

    std::future<int> r1 = pool.submitTask(sum1, 1, 2);
    std::future<int> r1 = pool.submitTask(sum2, 1, 2, 3);
    std::future<int> r1 = pool.submitTask([](int b, int e) {
        int sum = 0;
        for (int i = b; i < e; ++i) {
            sum += i;
        }
        return sum;
    }, 1, 100);
    std::cout << r1.get() << std::endl;

    getchar();
}
