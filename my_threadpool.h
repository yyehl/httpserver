#ifndef _MY_THREADPOOL_H_
#define _MY_THREADPOOL_H_

#include <list>
#include <cstdio>
#include <exception>
#include <pthread.h>
#include "my_locker.h"

template <typename T>               // 参数T为任务类
class threadpool
{
public:
    threadpool(int thread_num = 8, int max_requests = 10000);  // 默认线程数为8，最大连接请求为10000
    ~threadpool();

    bool append(T* request);                    // 往请求队列添加任务请求的函数，仅有的除构造析构函数之外的 公开接口 ，
                                                // 只需要把任务加进来就行了
private:
    static void* worker(void* arg);             // 静态成员函数。工作线程运行的函数，不断的从请求队列中取出线程并运行
                                                // 注意worker函数一般来说，必须为静态成员函数
                                                // 因为worker要被传给pthread_create函数作为第三个参数，即线程启动函数
                                                // 而第四个函数就是传给线程启动函数的参数，需要手动指定
                                                // 然而如果worker不是静态成员函数，而是非静态成员函数，那么编译时会自动加上隐式this指针
                                                // 这将会导致线程创建失败，所以一般把类内的线程函数设为静态成员函数
                                                // 而且线程函数不应该是随着对象的消失而消失的，它应该是与进程同在的
                                                // 然后通过传入this指针，再调用下面的run函数，实际执行真正属于每个对象的操作
                                                
    void run();                                 // 实际运行的函数

private:
    int             m_thread_number;            // 线程池的线程数
    int             m_max_requests;             // 请求队列中允许的最大请求数
    pthread_t*      m_threads;                  // 线程池数组，大小为线程数
    std::list<T*>   m_workqueue;                // 任务请求队列
    mutex_locker    m_queuelocker;              // 保护任务请求队列的互斥锁
    sem             m_queuestat;                // 是否有任务需要处理
    bool            m_stop;                     // 是否结束线程
};

template<typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) :    // 构造函数
                          m_thread_number(thread_number), 
                          m_max_requests(max_requests),
                          m_threads(NULL), 
                          m_stop(false) 
{
    if (thread_number <= 0 || max_requests <= 0)            // 如果线程数与最大任务请求数不符合要求，则抛出异常
        throw std::exception();

    m_threads = new pthread_t[thread_number];               // 线程数组，后面的线程tid号都是存放在这个数组中
    if (!m_threads)                                         // 创建失败，抛出异常
        throw std::exception();
    
    for (int i = 1; i <= m_thread_number; i++)              // 创建m_thread_number个线程
    {
        printf("create the %dth thread\n", i);

        if (pthread_create(m_threads[i-1], NULL, worker, this) != 0)   // 创建线程，线程id存放于线程数组
                                                                       // 注意，给worker传递的参数是 this，即指针对象本身的指针
                                                                       // 因为worker是必须设置为静态成员函数，否则不能通过
        {                                                              // 因为非静态函数会自动加一个this指针，导致编译无法通过
                                                                       // 线程只要初始化了，就已经在worker函数那里等着了 
            delete [] m_threads;           // 若线程创建失败，则释放之前申请的动态数组，后抛出异常
            throw std::exception();        
        }
        if (pthread_detach(m_threads[i-1]) != 0)       // 将每个新创建的线程设置为分离属性，这样就不需要其他线程等待它的结束
        {
            delete [] m_threads;            // 如果设置失败，先释放之前申请的动态内存，后抛出异常
            throw std::exception();
        }
    }
}

template<typename T>
threadpool<T>::~threadpool()
{
    delete [] m_threads;        // 析构函数，释放申请的动态数组后，将m_stop设为true，这样所有的线程都会停止运行
    m_stop = true;
}

template<typename T>
bool threadpool<T>::append(T* request)     // 往任务请求队列中添加请求
{       
    m_queuelocker.lock();                   // 操作之前需要对队列上锁
    if (m_workqueue.size() >= m_max_requests)   //如果已经达到最大的任务请求数了，则忽略请求，返回false
    {
        m_queuelocker.unlock();
        return false;
    }
    m_workqueue.push_back(request);     // 添加任务请求
    m_queuelocker.unlock();             // 解锁
    m_queuestat.post();                 // 通知线程池，有任务请求到了，快来处理
    return true;
}

template<typename T>
void* threadpool<T>::worker(void* arg)    // 线程池工作函数，内部调用run函数
{
    threadpool* pool = (threadpool*)arg;  // 将传进来的this指针转回threadpool对象指针，调用相应的成员函数进行处理
    pool->run();
    return pool;
}

template<typename T>
void threadpool<T>::run()                 // 线程池的实际工作函数
{
    while (!m_stop)                       // 只要m_stop没有设为停止，则不断循环
    {
        m_queuestat.wait();               // 阻塞于任务请求队列，若有任务了，将会被唤醒
                                          // 线程池启动之后，在没有任务来之前，全部线程阻塞于此
                                          // 当有任务来了之后，服务器主线程调用append添加任务
                                          // 然后append函数会发信号通知正在wait的线程
                                          // 然后正在wait的函数通过竞争上岗，谁抢到谁上
                                          
        m_queuelocker.lock();             // 唤醒后，先上锁
        if (m_workqueue.empty())          // 如果发现队列是空的，那就跳出本次循环，继续等待请求队列
        {
            m_queuelocker.unlock();
            continue;
        }
        T* request = m_workqueue.front();  // 如果有任务请求，则取出
        m_workqueue.pop_front();
        m_queuelocker.unlock();
        if (!request)                      // 如果取出后发现是空的。。。就继续等待。。。
            continue;   
        request->process();                // 如果是正常的任务请求，则处理，这是任务类提供的处理接口
    }
}

#endif
