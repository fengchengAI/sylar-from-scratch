/**
 * @file scheduler.cc
 * @brief 协程调度器实现
 * @version 0.1
 * @date 2021-06-15
 */
#include "scheduler.h"
#include "macro.h"
#include "hook.h"

namespace sylar {

static sylar::Logger::ptr g_logger = SYLAR_LOG_NAME("system");

/// 当前线程的调度器，主要是为了使用GetThis（）。
Scheduler * t_scheduler = nullptr;

/// 当前线程的调度协程，每个线程都独有一份
static thread_local Fiber * t_scheduler_fiber = nullptr;

Scheduler * Scheduler::GetThis() {
    return t_scheduler;
}

Scheduler::Scheduler(size_t threads, const std::string &name) : m_name(name), m_threadCount(threads) {
    SYLAR_ASSERT(threads > 0);
    setThis();
}

Fiber *Scheduler::GetMainFiber() { 
    return t_scheduler_fiber;
}

Scheduler::~Scheduler() {
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::~Scheduler()";
    SYLAR_ASSERT(m_stopping);
    if (GetThis() == this) {
        t_scheduler = nullptr;
    }
}

void Scheduler::start() {
    SYLAR_LOG_DEBUG(g_logger) << "start";
    // MutexType::Lock lock(m_mutex);
    if (m_stopping) {
        SYLAR_LOG_ERROR(g_logger) << "Scheduler is stopped";
        return;
    }
    SYLAR_ASSERT(m_threads.empty());
    m_threads.resize(m_threadCount);

    for (size_t i = 0; i < m_threadCount; i++) {
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run, this),
                                      m_name + "_" + std::to_string(i)));
        m_threadIds.push_back(m_threads[i]->getId());
    }
}

bool Scheduler::stopping() {
    // MutexType::Lock lock(m_mutex);
    return m_stopping && m_tasks.empty() && m_activeThreadCount == 0;
}

void Scheduler::tickle() { 
    SYLAR_LOG_DEBUG(g_logger) << "ticlke"; 
}

void Scheduler::idle() {
    SYLAR_LOG_DEBUG(g_logger) << "idle";
    while (!stopping()) {
        sylar::Fiber::GetThis()->yield();
    }
}

void Scheduler::stop() {
    SYLAR_LOG_DEBUG(g_logger) << "stop";
    if (stopping()) {
        return;
    }
    m_stopping = true;

    for (size_t i = 0; i < m_threadCount; i++) {
        tickle();
    }

    for (auto &i : m_threads) {
        i->join();
    }
}
void Scheduler::setThis(){
    t_scheduler = this;
}

void Scheduler::run() {
    SYLAR_LOG_DEBUG(g_logger) << "run";
    set_hook_enable(false);

    t_scheduler_fiber = sylar::Fiber::GetThis().get();  // 根协程。

    Fiber::ptr idle_fiber(new Fiber(std::bind(&Scheduler::idle, this)));
    Fiber::ptr cb_fiber;

    std::shared_ptr<ScheduleTask> task;
    while (true) {
        std::cout<<"Scheduler::run()-------------------------"<<std::endl;
        // task->reset();
        bool tickle_me = false; // 是否tickle其他线程进行任务调度
        {
            // MutexType::Lock lock(m_mutex);
            auto it = m_tasks.begin();
            // 遍历所有调度任务
            while (it != m_tasks.end()) {
                if ((*it)->isLock)
                    continue;
                (*it)->isLock = true;

                if ((*it)->thread != -1 && (*it)->thread != sylar::GetThreadId()) {
                    // 指定了调度线程，但不是在当前线程上调度，标记一下需要通知其他线程进行调度，然后跳过这个任务，继续下一个
                    ++it;
                    tickle_me = true;
                    continue;
                }

                // 找到一个未指定线程，或是指定了当前线程的任务
                SYLAR_ASSERT((*it)->fiber || (*it)->cb);
                if ((*it)->fiber) {
                    // 任务队列时的协程一定是READY状态，谁会把RUNNING或TERM状态的协程加入调度呢？
                    SYLAR_ASSERT((*it)->fiber->getState() == Fiber::READY);
                }
                // 当前调度线程找到一个任务，准备开始调度，将其从任务队列中剔除，活动线程数加1
                task = *it;
                m_tasks.erase(it++);
                ++m_activeThreadCount;
                break;
            }
            // 当前线程拿完一个任务后，发现任务队列还有剩余，那么tickle一下其他线程
            tickle_me |= (it != m_tasks.end());
        }

        if (tickle_me) {
            tickle();
        }

        if (task&&task->fiber) {
            // resume协程，resume返回时，协程要么执行完了，要么半路yield了，总之这个任务就算完成了，活跃线程数减一
            task->fiber->resume();
            --m_activeThreadCount;
            task->reset();
        } else if (task&&task->cb) {
            //if (cb_fiber) {
            //    cb_fiber->reset(task->cb);
            //} else {
            cb_fiber.reset(new Fiber(task->cb));
            //}
            task->reset();
            cb_fiber->resume();
            --m_activeThreadCount;
            cb_fiber.reset();
        } else {
            // 进到这个分支情况一定是任务队列空了，调度idle协程即可
            if (idle_fiber->getState() == Fiber::TERM) {
                // 如果调度器没有调度任务，那么idle协程会不停地resume/yield，不会结束，如果idle协程结束了，那一定是调度器停止了
                SYLAR_LOG_DEBUG(g_logger) << "idle fiber term";
                break;
            }
            ++m_idleThreadCount;
            idle_fiber->resume();
            --m_idleThreadCount;
        }
    }
    SYLAR_LOG_DEBUG(g_logger) << "Scheduler::run() exit";
}

} // end namespace sylar