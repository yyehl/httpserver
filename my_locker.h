#ifndef _MY_LOCKER_H_
#define _MY_LOCKER_H_

#include <pthread.h>
#include <exception>
#include <semaphore.h>

class sem
{
public:
    sem()
    {
        if (sem_init(&m_sem, 0, 0) != 0)  // 信号量直接初始化为0
            throw std::exception();       // 当初始化出现错误时，抛出异常
    }                                     // 因为现在没有已经被初始化的数据成员，所以现在抛出异常是安全的，不会内存泄露
    ~sem()
    {
        sem_destroy(&m_sem);             // 析构函数释放内存
    }
    bool wait()
    {
        return sem_wait(&m_sem) == 0;   // 若原本信号量计数大于0，则wait操作使得计数-1，表示成功调用，sem_wait返回0，wait返回true
    }                                   // 若原本信号量计数小于等于0，则将会阻塞于wait操作，若出错，sem_wait返回-1，wait返回false
    bool post()
    {                                   // 若原本有进程阻塞于该信号量，则post操作将计数+1，之后会唤醒一个阻塞的进程，马上又会被-1
        return sem_post(&m_sem) == 0;   // 调用成功sem_post返回0，post则返回true，失败sem_post返回-1，post返回false
    }
private:
    sem_t m_sem;                        // 封装semaphore信号量为内部数据成员
};


class mutex_locker
{
public: 
    mutex_locker()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
            throw std::exception();    // 初始化mutex，若失败抛出异常，因为之前没有已初始化成员，故不会内存泄露
    }
    ~mutex_locker()
    {
        pthread_mutex_destroy(&m_mutex);     //析构函数释放内存
    }
    bool lock()
    {
        return pthread_mutex_lock(&m_mutex) == 0;    // 成功上锁返回true，不然则阻塞，若调用失败则返回false
    }
    bool unlock()
    {
        return pthread_mutex_unlock(&m_mutex) == 0;  // 成功解锁返回true，调用失败返回false
    }
private:
    pthread_mutex_t m_mutex;                         // 封装mutex为数据成员
};

class cond
{
public:
    cond()
    {
        if (pthread_mutex_init(&m_mutex, NULL) != 0)
            throw std::exception();                  // 对mutex初始化，之前没有成员被初始化了，所以抛异常不会出现内存泄露
        if (pthread_cond_init(&m_cond, NULL) != 0)
        {
            pthread_mutex_destroy(&m_mutex);     // 之前mutex已经初始化了，若此时初始化出错，则需要将之前的先释放
            throw std::exception();
        }
    }
    ~cond()
    {
        pthread_mutex_destroy(&m_mutex);        // 析构函数释放内存
        pthread_cond_destroy(&m_cond);
    }
    bool wait()
    {
        int ret = 0;                                        
        pthread_mutex_lock(&m_mutex);                     // 先对条件变量上锁，这样其他线程暂时不能改变条件变量
        ret = pthread_cond_wait(&m_cond, &m_mutex);       // 判断条件变量是否满足条件，若无法满足，解锁，并阻塞
        pthread_mutex_unlock(&m_mutex);                   // 对条件变量解锁
        return ret == 0;
    }
    bool signal()
    {
        return pthread_cond_signal(&m_cond) == 0;         // 给阻塞在条件变量上的一个线程发信号
    }

private:
    pthread_mutex_t m_mutex;                              // 条件变量的正常使用需要与mutex配合
    pthread_cond_t m_cond;
};

#endif