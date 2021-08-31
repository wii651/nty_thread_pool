/***************************************************************
 * 
 * @file:    quick_start.c
 * @author:  wilson
 * @version: 1.0
 * @date:    2021-08-21 14:16:08
 * @license: MIT
 * @brief:   
 * 编译:
 * 	gcc quick_start.c nty_thread_pool.c -pthread
 * 
 ***************************************************************/

#include <stdio.h>

#include "nty_thread_pool.h"

void task_func(void *arg){
	printf("doing business[%d]\n", (int)arg);
}

int main(){

	// 创建拥有3个工作线程的线程池
	ThreadPool* pool = threadpool_init(3);

	// 将100个任务交给线程池
	int i;
	for (i = 0; i < 100; i++){
		threadpool_add_task(pool, task_func, (void*)i);
	};
	
    // 阻塞等待直到线程池中所有任务完成
	threadpool_wait_tasks_finish(pool);
	
    // 销毁线程池
	threadpool_destroy(pool);
	
	return 0;
}