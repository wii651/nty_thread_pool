/***************************************************************
 * 
 * @file:    performance_test.c
 * @author:  wilson
 * @version: 1.0
 * @date:    2021-08-10 21:06:31
 * @license: MIT
 * @brief:   
 * 编译:
 * 		gcc (-g) performance_test.c nty_thread_pool.c (-D WII_DEBUG) -pthread
 * 
 * 执行:
 * 		./a.out [线程池线程数] [任务数]
 * 
 ***************************************************************/

#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>


#include "nty_thread_pool.h"

void task_func(void *arg){
	printf("doing business[%d]\n", (int)arg);
}


int main(int argc, char** argv){

	if (argc < 3) {
		printf("error input param number\n");
		exit(1);
	}
	printf("task_num: %d, thread_num: %d\n", atoi(argv[1]), atoi(argv[2]));
	clock_t start, end;
	double duration;
	start = clock();	

	//puts("Making threadpool with argv[1] threads");
	ThreadPool* pool = threadpool_init(atoi(argv[1]));

	//puts("Adding argv[2] tasks to threadpool");
	int i;
	for (i = 0; i < atoi(argv[2]); i++){
		threadpool_add_task(pool, task_func, (void*)i);
	};

	threadpool_wait_tasks_finish(pool);

	end = clock();
	duration = ((double)(end - start)) / CLOCKS_PER_SEC;

	printf("time cost: %lf\n", duration);

	puts("Killing threadpool");
	threadpool_destroy(pool);
	
	return 0;
}