/**
 * @file test_scheduler.cc
 * @brief 协程调度器测试
 * @version 0.1
 * @date 2021-06-15
 */

#include "sylar/sylar.h"
#include <thread>
#include <chrono>
static sylar::Logger::ptr g_logger = SYLAR_LOG_ROOT();

/**
 * @brief 演示协程主动yield情况下应该如何操作
 */
void test_fiber1() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber1 begin";

    // 注释下面的函数，则这个函数执行到yield就推出了，如果有这个函数就可以执行完。WHY ？
    // 为什么需要下面一个这样的函数，因为默认要执行test_fiber1函数，就会使用一个fiber包装这个cb，
    // 于是此时的sylar::Fiber::GetThis()已经是这个test_fiber1所在的那个协程了，而不是根携程
    // 当sylar::Fiber::GetThis()->yield();执行的时候会回到根协程，且保存了这个test_fiber1这个函数的现场，（下一行执行 “SYLAR_LOG_INFO(g_logger) << "after test_fiber1 yield";”）
    // 又因为上面把当前协程又注册了一次，下次再运行的时候会又会继续resume，
    // 但是这样是由问题的，假设sylar::Scheduler::GetThis()->schedule(sylar::Fiber::GetThis());和sylar::Fiber::GetThis()->yield();中间有较多的任务。
    //导致下一个调度期resume sylar::Fiber::GetThis()这个协程的时候，还没有执行到sylar::Fiber::GetThis()->yield();，那么......噩梦的开始。
    // 所以禁止函数内部调用yield。 禁止协程创建协程（因为这个是非对称）。


    sylar::Scheduler::GetThis()->schedule(sylar::Fiber::GetThis());
    SYLAR_LOG_INFO(g_logger) << "before test_fiber1 yield";
    sylar::Fiber::GetThis()->yield();
    SYLAR_LOG_INFO(g_logger) << "after test_fiber1 yield";

    SYLAR_LOG_INFO(g_logger) << "test_fiber1 end";
}

/**
 * @brief 演示协程睡眠对主程序的影响
 */
void test_fiber2() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber2 begin";

    /**
     * 一个线程同一时间只能有一个协程在运行，线程调度协程的本质就是按顺序执行任务队列里的协程
     * 由于必须等一个协程执行完后才能执行下一个协程，所以任何一个协程的阻塞都会影响整个线程的协程调度，这里
     * 睡眠的3秒钟之内调度器不会调度新的协程，对sleep函数进行hook之后可以改变这种情况
     */
    // sleep(3);

    SYLAR_LOG_INFO(g_logger) << "test_fiber2 end";
}

void test_fiber3() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber3 begin";
    SYLAR_LOG_INFO(g_logger) << "test_fiber3 end";
}

void test_fiber5() {
    static int count = 0;

    SYLAR_LOG_INFO(g_logger) << "test_fiber5 begin, i = " << count;
    SYLAR_LOG_INFO(g_logger) << "test_fiber5 end i = " << count;

    count++;
}

/**
 * @brief 演示指定执行线程的情况
 */
void test_fiber4() {
    SYLAR_LOG_INFO(g_logger) << "test_fiber4 begin";
    
    for (int i = 0; i < 3; i++) {
        sylar::Scheduler::GetThis()->schedule(test_fiber5, sylar::GetThreadId());
    }

    SYLAR_LOG_INFO(g_logger) << "test_fiber4 end";
}

int main() {
    SYLAR_LOG_INFO(g_logger) << "main begin";

    /** 
     * 只使用main函数线程进行协程调度，相当于先攒下一波协程，然后切换到调度器的run方法将这些协程
     * 消耗掉，然后再返回main函数往下执行
     */
    sylar::Scheduler sc(1, "test");

    // 额外创建新的线程进行调度，那只要添加了调度任务，调度器马上就可以调度该任务
    // sylar::Scheduler sc(3, false);

    // 添加调度任务，使用函数作为调度对象
    sc.schedule(test_fiber1);
     sc.schedule(test_fiber2);
     sc.schedule(test_fiber4);
    // 添加调度任务，使用Fiber类作为调度对象
     sylar::Fiber::ptr fiber(new sylar::Fiber(&test_fiber3));
     sc.schedule(fiber);

    // 创建调度线程，开始任务调度，如果只使用main函数线程进行调度，那start相当于什么也没做
    sc.start();

    /**
     * 只要调度器未停止，就可以添加调度任务
     * 包括在子协程中也可以通过sylar::Scheduler::GetThis()->scheduler()的方式继续添加调度任务
     */
    // sc.schedule(test_fiber4);

    /**
     * 停止调度，如果未使用当前线程进行调度，那么只需要简单地等所有调度线程退出即可
     * 如果使用了当前线程进行调度，那么要先执行当前线程的协程调度函数，等其执行完后再返回caller协程继续往下执行
     */
    sc.stop();

    SYLAR_LOG_INFO(g_logger) << "main end";
    return 0;
}