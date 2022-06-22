#ifndef TINYSERVER_THREADPOOL_H
#define TINYSERVER_THREADPOOL_H

#include <queue>
#include <mutex>
#include <condition_variable>

class Runner;

class ThreadPool {
public:
    ThreadPool(int thread_num = 16, int max_wait_task = 23333);
    ~ThreadPool();

    bool appendTask(Runner *runner);

private:
    static void *worker(void *arg);
    void run();

private:
    int m_thread_num;
    int m_max_wait_task;

    std::queue<Runner *> m_task_queue;
    std::mutex m_queue_mutex;
    std::condition_variable m_cv;
    bool m_stop;
};

#endif //TINYSERVER_THREADPOOL_H
