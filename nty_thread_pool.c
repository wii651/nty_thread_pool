/***************************************************************
 * 
 * @file:    nty_thread_pool.c
 * @author:  wilson
 * @version: 1.0
 * @date:    2021-08-24 14:43:05
 * @license: MIT
 * @brief:   线程池实现
 * 
 ***************************************************************/


#include "nty_thread_pool.h"

#ifdef __cpluscplus
extern "C" {
#endif


/* ============================ VARIABLES ============================ */

static volatile int threads_keepalive;
static volatile int threads_on_hold;

/* ============================ STRUCTURES ============================*/

/**
 * @brief 条件锁信号量
 */
typedef struct CondSem {
	pthread_mutex_t mutex;
	pthread_cond_t cond;
	int status;				//[ 条件等待的循环条件, 为0时表示循环等待, 为1时跳出循环 ]
} CondSem;

/**
 * @brief 任务
 */
typedef struct Task {
	struct Task* prev;
	void (*func)(void* arg);	//[ 实际业务函数指针 ]
	void* func_arg;				//[ 事业业务函数的传参 ]
} Task;

/**
 * @brief 任务队列
 */
typedef struct TaskQueue {
	pthread_mutex_t rw_lock;	//[ 操作队列时的互斥锁 ]
	Task* front;				//[ 队首节点指针 ]
	Task* rear;					//[ 队尾节点指针 ]
	CondSem* has_jobs;			//[ 通过该条件信号量, 唤醒工作线程去实现任务 ]
	int queue_len;				//[ 队列长度 ]
} TaskQueue;

/**
 * @brief 工作线程
 */
typedef struct Thread {
	int id;						//[ friendly thread id ]
	pthread_t thread_id;		//[ pointer to actual thread ]
	struct PoolManager* pool;
} Thread;

/**
 * @brief 线程池管理员
 */
typedef struct PoolManager {
	Thread** pthread_list;				//[ 指向thread指针的指针, 实质是个很隐蔽的链表 *需要单独free* ]
	volatile int num_threads_alive;		//[ 当前存活的thread数量 ]
	volatile int num_threads_working;	//[ 当前工作的thread数量 ]
	pthread_mutex_t thread_num_lock;	//[ 当修改num_threads_alive或num_threads_working时用的锁 ]
	pthread_cond_t threads_all_idle;	//[ 用于实现WaitTasksFinish的条件等待变量 ]
	TaskQueue task_queue;
} PoolManager;

/* ============================ PROTOTYPES ============================ */

static int thread_init(PoolManager* pool, Thread** pthread_node, int id);
static void* thread_routine(Thread* thread_node);
static void thread_hold(int sig_id);
static void thread_destroy(Thread* thread_node);

static int taskqueue_init(TaskQueue* queue);
static void taskqueue_clear(TaskQueue* queue);
static void taskqueue_push(TaskQueue* queue, Task* newtask);
static Task* taskqueue_pull(TaskQueue* queue);
static void taskqueue_destroy(TaskQueue* queue);

static void sem_init(CondSem* sem, int wake_up);
static void sem_reset(CondSem* sem);
static void sem_post_one(CondSem* sem);
static void sem_post_all(CondSem* sem);
static void sem_wait(CondSem* sem);

/* ============================ THREAD POOL ============================ */


ThreadPool* threadpool_init(int num_threads) {
	
	threads_keepalive = 1;
	threads_on_hold = 0;
	if (num_threads < 0) {
		num_threads = 0;
	}

	//[ 创建线程池实体 ]
	PoolManager* pool = (PoolManager*)calloc(1, sizeof(PoolManager));
	if (pool == NULL) {
		err("ThreadPoolInit(): Could not allocate memory for thread pool\n");
		return NULL;
	}
	pool->num_threads_alive = 0;
	pool->num_threads_working = 0;

	//[ 创建任务队列实体(未包含工作线程链表实体) ]
	if (taskqueue_init(&pool->task_queue) == -1) {
		err("ThreadPoolInit(): Could not allocate memory for task queue\n");
		free(pool);
		return NULL;
	}
	
	//[ 创建工作线程链表实体, 即n个指向线程实体的指针 ]
	pool->pthread_list = (Thread**)calloc(num_threads, sizeof(Thread*));
	if (pool->pthread_list == NULL) {
		err("ThreadPoolInit(): Could not allocate memory for all threads\n");
		taskqueue_destroy(&pool->task_queue);
		free(pool);
		return NULL;
	}

	//[ 初始化线程池相关锁 ]
	pthread_mutex_init(&pool->thread_num_lock, NULL);
	pthread_cond_init(&pool->threads_all_idle, NULL);

	//[ 线程实体创建 ]
	for (int n = 0; n < num_threads; n++) {
		thread_init(pool, &(pool->pthread_list[n]), n);
#if WII_DEBUG
		printf("WII_DEBUG: Created thread %d in pool\n", n);
#endif
	}

	//[ 等待所有线程创建完毕 ]
	while (pool->num_threads_alive != num_threads) { }

	return pool;
}


int threadpool_add_task(ThreadPool* pool, void (*func)(void*), void* func_arg) {	
	//[ 构造新任务实体 ]
	Task* new_task = (Task*)calloc(1, sizeof(Task));
	if (new_task == NULL) {
		err("AddTaskToPool(): Could not allocate memory for new task\n");
		return -1;
	}

	new_task->func = func;
	new_task->func_arg = func_arg;

	//[ 加入任务队列 ]
	taskqueue_push(&pool->task_queue, new_task);

	return 0;
}


void threadpool_wait_tasks_finish(ThreadPool* pool) {
	pthread_mutex_lock(&pool->thread_num_lock);
	/*
		当所有任务都完成 且 没有正在工作的线程时, 才会唤醒
		换言之 有任务未完成 或 有正在工作的线程时, 都进入条件等待
		条件唤醒在thread_routine()函数, 线程完成实际业务之后
	*/
	while (pool->task_queue.queue_len != 0 || pool->num_threads_working != 0) {
		pthread_cond_wait(&pool->threads_all_idle, &pool->thread_num_lock);
	}
	pthread_mutex_unlock(&pool->thread_num_lock);
}


void threadpool_pause_all_threads(ThreadPool* pool) {
	//[ 向每个线程发送SIGUSR1信号, 之后线程就会进入thread_hold()阻塞 ]
	threads_on_hold = 1;
	for (int n = 0; n < pool->num_threads_alive; n++) {
		pthread_kill(pool->pthread_list[n]->thread_id, SIGUSR1);
	}
}


void threadpool_resume_all_threads(ThreadPool* pool) {
	threads_on_hold = 0;
}


void threadpool_destroy(ThreadPool* pool) {
	if (pool == NULL) return;

	//[ 记录一下存活的线程数 ]
	volatile int threads_total = pool->num_threads_alive;

	//[ 结束工作线程的thread_routine中的infinite loop ]
	//[ 唤醒条件等待的线程们, 线程们开始自己回收结束 ]
	threads_keepalive = 0;
	sem_post_all(pool->task_queue.has_jobs);

	//[ 至少给1秒时间等待线程们自行回收 ]
	double TIMEOUT = 1.0;
	clock_t start, end;
	double duration = 0.0;
	start = clock();

	while (duration < TIMEOUT && pool->num_threads_alive != 0) {
		end = clock();
		duration = ((double)(end - start)) / CLOCKS_PER_SEC;	
	}

	
	while (pool->num_threads_alive != 0) {
		sem_post_all(pool->task_queue.has_jobs);
		sleep(1);
	}
#if WII_DEBUG
	printf("num_threads_alive is 0, start recycling resources\n");
#endif
	//[ 资源释放 ]
	taskqueue_destroy(&pool->task_queue);
	for (int i = 0; i < threads_total; i++) {
		thread_destroy(pool->pthread_list[i]);
	}
	free(pool->pthread_list);
	free(pool);
}


int threadpool_get_working_num(ThreadPool* pool) {
	return pool->num_threads_working;
}


/* ============================ THREAD ============================ */
/**
 * @brief 初始化一个线程, 并将其加入thread_list
 * 
 * @param pool[in] 要进行初始化的线程池句柄
 * @param pthread_node[out] 指向thread_list结点的指针的指针(二重指针实现传址调用)
 * @param id[in] friendly threadid
 * @return int -- 0 on success, -1 otherwise.
 */
static int thread_init(PoolManager* pool, Thread** pthread_node, int id) {
	/*
		**pthread_node:	链表节点本体
		*pthread_node:	指向链表头结点的指针, 步距为单个节点
		pthread_node:	指向*thread_list的指针, 只是为了传参时的传址调用, 本身没有意义
	*/
	*pthread_node = (Thread*)calloc(1, sizeof(Thread));
	if (*pthread_node == NULL) {
		err("thread_init(): Could not allocate memory for thread\n");
		return -1;
	}

	(*pthread_node)->pool = pool;
	(*pthread_node)->id = id;

	if (pthread_create(&(*pthread_node)->thread_id, NULL, 
			(void* (*)(void*))thread_routine, (*pthread_node)) != 0) {
		err("thread_init(): Could not create a new thread\n");
		return -1;
	}
	pthread_detach((*pthread_node)->thread_id);
	return 0;
}

/**
 * @brief 设置当前线程挂起, 通过SIGUSR1触发此函数
 * 
 * @param sig_id
 */
static void thread_hold(int sig_id) {
	while (threads_on_hold == 1) {
		sleep(1);
	}
}

/**
 * @brief 释放单个线程资源
 * 
 * @param thread_node
 */
static void thread_destroy(Thread* thread_node) {
	free(thread_node);
}

/**
 * @brief 工作线程的入口函数
 * 
 * @param thread_node
 * @return void*
 */
static void* thread_routine(Thread* thread_node) {
	//[ 重命名线程 ]
	char thread_name[32] = {0};
	snprintf(thread_name, 32, "thread-pool-%d", thread_node->id);

#if defined(__linux__)
	prctl(PR_SET_NAME, thread_name);
#elif defined(__APPLE__) && defined(__MACH__)
	pthread_setname_np(thread_name);
#else
	err("thread_routine(): pthread_setname_np is not support on this OS\n");
#endif

	//[ 从传参中获取上下文信息 ]
	PoolManager* pool = thread_node->pool;

	//[ 绑定信号量触发函数 ]
	struct sigaction act;
	sigemptyset(&act.sa_mask);
	act.sa_flags = 0;
	act.sa_handler = thread_hold;
	if (sigaction(SIGUSR1, &act, NULL) == -1) {
		err("thread_routine(): cannot bind function with SIGUSR1\n");
	}

	//[ mark thread as alive ]
	pthread_mutex_lock(&pool->thread_num_lock);
	pool->num_threads_alive++;
	pthread_mutex_unlock(&pool->thread_num_lock);

	//[ 线程任务主循环 ]
	while (threads_keepalive) {
		//[ 根据是否有任务分发, 条件等待 ]
		sem_wait(pool->task_queue.has_jobs);

		if (threads_keepalive) {
			pthread_mutex_lock(&pool->thread_num_lock);
			pool->num_threads_working++;
			pthread_mutex_unlock(&pool->thread_num_lock);

			Task* task = taskqueue_pull(&pool->task_queue);
			if (task) {
				//[ 执行真正的业务 ]
				task->func(task->func_arg);
				free(task);
			}

			pthread_mutex_lock(&pool->thread_num_lock);
			pool->num_threads_working--;

			//[ 判断是否所有线程都闲置 ]
			if (pool->num_threads_working == 0) {
				pthread_cond_signal(&pool->threads_all_idle);
			}
			pthread_mutex_unlock(&pool->thread_num_lock);
		}
	}

	//[ 线程退出 ]
	pthread_mutex_lock(&pool->thread_num_lock);
	pool->num_threads_alive--;
// #if WII_DEBUG
// 	printf("WII_DEBUG: this is thread %d. alive_num: %d\n", thread_node->id, pool->num_threads_alive);
// #endif
	pthread_mutex_unlock(&pool->thread_num_lock);

	return NULL;
}



/* ============================ TASK QUEUE ============================ */

static int taskqueue_init(TaskQueue* queue) {
	queue->queue_len = 0;
	queue->front = NULL;
	queue->rear = NULL;

	queue->has_jobs = (CondSem*)calloc(1, sizeof(CondSem));
	if (queue->has_jobs == NULL) {
		err("taskqueue_init(): cannot allocate memory for CondSem\n");
		return -1;
	}

	pthread_mutex_init(&queue->rw_lock, NULL);
	sem_init(queue->has_jobs, THREAD_KEEP_WAIT);

	return 0;
}


static void taskqueue_push(TaskQueue* queue, Task* newtask) {
	pthread_mutex_lock(&queue->rw_lock);

	if (queue->queue_len == 0) {
		queue->front = newtask;
		queue->rear = newtask;
	} else {
		queue->rear->prev = newtask;
		queue->rear = newtask;
	}
	queue->queue_len++;

	//[ 有任务了, 唤醒一下 ]
	if (queue->queue_len > 0)
		sem_post_one(queue->has_jobs);

	pthread_mutex_unlock(&queue->rw_lock);
}

static Task* taskqueue_pull(TaskQueue* queue) {
	pthread_mutex_lock(&queue->rw_lock);
	
	Task* retTask = queue->front;

	switch (queue->queue_len) {
		case 0:
			break;
		case 1:
			queue->queue_len--;
			queue->front = NULL;
			queue->rear = NULL;
			break;
		default:
			queue->queue_len--;
			queue->front = retTask->prev;
			sem_post_one(queue->has_jobs);
	}

	pthread_mutex_unlock(&queue->rw_lock);
	return retTask;
}


//[ 执行前, 先销毁所有子线程, 因此不存在数据同步问题 ]
static void taskqueue_destroy(TaskQueue* queue) {
	while (queue->queue_len) {
		free(taskqueue_pull(queue));
	}
	//[ 在任务队列为空以后, 不会再存在任务线程与主线程抢夺资源的情况,  ]

	queue->front = NULL;
	queue->rear = NULL;
	queue->queue_len = 0;
	sem_reset(queue->has_jobs);

	free(queue->has_jobs);
}


/* ============================ SYNCHRONIZATION ============================ */
/**
 * @brief 初始化信号量, 并设置信号量(0/1/2)
 * 
 * @param sem_p[out] 信号量指针
 * @param status[in] 设置值, 需为THREAD_KEEP_WAIT(0)/THREAD_WAKE_ONE(1)/THREAD_WAKE_ALL(2)
 */
static void sem_init(CondSem* sem, int status) {
	if (!(status == 0 || status == 1 || status == 2)) {
		err("sem_init(): Conditional Semaphore can take only values 2 or 1 or 0");
		exit(1);
	}
	pthread_mutex_init(&sem->mutex, NULL);
	pthread_cond_init(&sem->cond, NULL);
	sem->status = status;
}


/**
 * @brief 初始化信号量, 并将信号量设为0
 * 
 * @param sem
 */
static void sem_reset(CondSem* sem) {
	sem_init(sem, THREAD_KEEP_WAIT);
}

/**
 * @brief 唤醒至少一个条件等待线程
 * 
 * @param sem
 */
static void sem_post_one(CondSem* sem) {
	pthread_mutex_lock(&sem->mutex);
	sem->status = THREAD_WAKE_ONE;					//[ 解除while条件 ]
	pthread_cond_signal(&sem->cond);				//[ 单个唤醒 ]
	pthread_mutex_unlock(&sem->mutex);
}
/**
 * @brief 唤醒所有条件等待的线程
 * 
 * @param sem
 */
static void sem_post_all(CondSem* sem) {
	pthread_mutex_lock(&sem->mutex);
	sem->status = THREAD_WAKE_ALL;					//[ 解除while条件 ]
	pthread_cond_broadcast(&sem->cond);				//[ 唤醒全部 ]
	pthread_mutex_unlock(&sem->mutex);
}

/**
 * @brief 执行条件等待
 * 
 * @param sem
 */
static void sem_wait(CondSem* sem) {
	pthread_mutex_lock(&sem->mutex);
	while (sem->status == THREAD_KEEP_WAIT) {
		pthread_cond_wait(&sem->cond, &sem->mutex);
	}
	//[ 对于THREAD_WAKE_ONE的情况, 修正status保证只唤醒一个. 对于THREAD_WAKE_ALL则不用处理 ]
	if (sem->status == THREAD_WAKE_ONE) {
		sem->status = THREAD_KEEP_WAIT;
	}
	pthread_mutex_unlock(&sem->mutex);
}

#ifdef __cpluscplus
}
#endif