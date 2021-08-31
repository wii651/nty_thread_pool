/***************************************************************
 * 
 * @file:    nty_thread_pool.h
 * @author:  wilson
 * @version: 1.0
 * @date:    2021-08-06 16:22:16
 * @license: MIT
 * @brief:   线程池实现
 * 
 ***************************************************************/


#ifndef _NTY_THREAD_POOL_H_ 
#define _NTY_THREAD_POOL_H_ 

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <signal.h>
#include <errno.h>

#if defined(__linux__)
	#include <sys/prctl.h>
#endif

#ifdef WII_DEBUG
	#define WII_DEBUG 1
#else
	#define WII_DEBUG 0
#endif

#if !defined(DISABLE_PRINT) || defined(WII_DEBUG)
	#define err(str)	fprintf(stderr, str)
#else
	#define err(str)
#endif

#define THREAD_KEEP_WAIT	0
#define THREAD_WAKE_ONE		1
#define THREAD_WAKE_ALL		2

#ifdef __cpluscplus
extern "C" {
#endif

/* ============================ API ============================ */

typedef struct PoolManager ThreadPool;


/**
 * @brief 创建线程池
 * 
 * @param num_threads[in] 初始线程吃数量 
 * @return ThreadPool* -- 创建成功返回线程池句柄(需要free), 失败返回NULL
 */
ThreadPool* threadpool_init(int num_threads);

/**
 * @brief 
 * 
 * @param pool[in] 线程池句柄
 * @param pFunc[in] 实际业务函数的指针
 * @param pFunc_arg[in] 实际业务函数(func)的传参
 * @return int -- 0 on success, -1 otherwise.
 */
int threadpool_add_task(ThreadPool* pool, void (*func)(void*), void* func_arg);

/**
 * @brief 等待, 直到队列中的所有task完成
 * 
 * @param pool[in] 线程池句柄
 */
void threadpool_wait_tasks_finish(ThreadPool* pool);

/**
 * @brief 立刻暂停所有工作线程. 
 * 在暂停期间, 可以添加新的task.
 * 
 * @param pool[in] 线程池句柄
 */
void threadpool_pause_all_threads(ThreadPool* pool);

/**
 * @brief 恢复所有工作线程的运行. 
 * 与wtp_pause配合使用. 
 * 
 * @param pool[in] 线程池句柄
 */
void threadpool_resume_all_threads(ThreadPool* pool);

/**
 * @brief 释放线程池
 * 
 * @param pool[in] 线程池句柄
 */
void threadpool_destroy(ThreadPool* pool);

/**
 * @brief 获取当前正在工作的线程数量
 * 
 * @param pool[in] 线程池句柄
 * @return int -- 正在工作的线程池数量
 */
int threadpool_get_working_num(ThreadPool* pool);


#ifdef __cpluscplus
}
#endif

#endif	// _NTY_THREAD_POOL_H_