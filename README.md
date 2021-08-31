# nty_thread_pool
轻量级ANSI C线程池.
* 简单易用
* 支持对线程池的pause/resume/wait操作
* 代码参考 https://github.com/Pithikos/C-Thread-Pool/

## quick start
```c
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
```

## 编译

使用下述语句进行编译:

```
gcc -g quick_start.c nty_thread_pool.c -pthread
```

使用下述语句编译获得更多调试信息:
```
gcc -g quick_start.c nty_thread_pool.c -D WII_DEBUG -pthread
```