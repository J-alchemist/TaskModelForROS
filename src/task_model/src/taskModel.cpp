#include <iostream>
#include <queue>
#include <unistd.h>
#include <pthread.h>
// #include <thread>
#include <functional>

#include <ros/ros.h>
#include <std_msgs/Float32.h>

// C语言函数指针实现
// template<class T>	// 任务参数模板化
// class threadTask { 
// 	public:
// 		typedef void(* handler_t )(T );

// 	private:
// 		T _data;				
// 		handler_t _handler;

// 	public:
// 		void setTask (const T& data, const handler_t& handler) {
// 			_data = data;
// 			_handler = handler;
// 		}
// 		void exec() {
// 			_handler(_data) ;
// 		}
// };

// C++特性function实现
template<class T>		// 任务参数模板化
class threadTask {

	private:
		T _data;				
		std::function<void(T )> _handler;

	public:
		void setTask (const T& param, const auto& handler) {
			_data = param;
			_handler = handler;
		}
		void exec() {
			_handler(_data);
		}
};


template<class T>
class blockingQueue {			// 阻塞队列: 

	private:
		std::queue<threadTask<T>> que;
		size_t capacity ;
		pthread_mutex_t mutex;
		pthread_cond_t cus_cond, pro_cond;

	public:
		blockingQueue(size_t s=5): capacity(s) {
			pthread_mutex_init ( &mutex, NULL);
			pthread_cond_init ( &cus_cond, NULL);
			pthread_cond_init ( &pro_cond, NULL);
		}
		~blockingQueue() {
			pthread_mutex_destroy(&mutex) ;
			pthread_cond_destroy(&cus_cond);
			pthread_cond_destroy(&pro_cond);
		}

		bool pushBQ(const threadTask<T>& data){

			pthread_mutex_lock(&mutex);
			while(que.size() >= capacity){
				pthread_cond_wait( &pro_cond, &mutex);
			}

			que.push(data);				// 任务入队

			pthread_cond_signal ( &cus_cond ) ;
			pthread_mutex_unlock ( &mutex );
			return true;
		}

		bool popBQ(threadTask<T>* data){

			pthread_mutex_lock( &mutex) ;
			while(que.empty() ){

				pthread_cond_wait( &cus_cond ,&mutex);
			}

			*data = que.front();		// 取出任务，执行
			que.pop();

			pthread_cond_signal( &pro_cond );
			pthread_mutex_unlock ( &mutex);
			return true;
		}
};

template<class T>
class threadPools {				// 基于阻塞队列的线程池
	public:	
		static void* threadEntry(void* arg) {
			threadPools* p = (threadPools *) arg; 

			while(1) {
				threadTask<T> task;		// 取任务执行
				p->_queue.popBQ(&task);
				task.exec();
			}
			return NULL;
		}

		threadPools(int max_poools_num = 5, int queue= 10)
							: _max_pool_nums (max_poools_num) ,
							  _queue(queue) {

			pthread_t tid[_max_pool_nums];
			printf("Thread pool nums: %d\n", _max_pool_nums);
			printf("Task queue size: %d\n", queue);

			for ( int i=0 ; i<_max_pool_nums ;++i) {

				if (pthread_create(&tid[i], NULL, &threadPools::threadEntry, this) == 0) {

					pthread_detach(tid[i]);
					printf("thread%d pid: %ld\n", i+1, tid[i]);
				} 
				else 
					exit(-1);

				// // C++ thread lamda
				// std::thread([=] ()-> void {
				// // ... ...	
				// }).detach() ;
			}
		}

		~threadPools() {
			printf("thread pools free...\n");
		}

		// 放入任务队列
		bool taskPush(const threadTask<T>& task) {
			return _queue.pushBQ(task);
		}

	private:
		int _max_pool_nums;
		blockingQueue<T> _queue;

};

class ros1Task {			// ros平台封装的任务类

	public:
		struct taskInfo {		// 任务参数
			int task_id;
			bool is_exec;
			float s_start;
			float s_end;
		
			taskInfo() = default;
			taskInfo(int id, bool exec_flag, float start, float end) 
												:   task_id(id),
													is_exec(exec_flag),
													s_start(start),
													s_end(end) { }
		};	
		using Type = taskInfo;

		ros1Task(int pool_size, int taskQueue_size) {

			// 线程池创建, 等待任务
			pool = std::make_shared<threadPools<Type>>(pool_size, taskQueue_size);

			// 外部接口：订阅话题, 进入回调发布任务
			taskInfo_sub = nh.subscribe("task_exec_s", 1, &ros1Task::taskInfoCallback, this);

			// 外部模拟：发布"task_exec_s", 任务放入队列
			taskInfo_pub = nh.advertise<std_msgs::Float32>("task_exec_s", 1);
		}

	public:
		ros::NodeHandle nh;
		ros::Subscriber taskInfo_sub;
		ros::Publisher taskInfo_pub;

		// 任务入队
		void taskInfoCallback(const std_msgs::Float32::ConstPtr& msg) {
			
			float cur_loc = msg->data;
			// 遍历任务表
			for (const auto& info : taskListInfo) {
				if (info.s_start < cur_loc  && cur_loc < info.s_end) {
					if (!std::get<1>(taskList[info.task_id])) {

						// 放入该任务进队列，进入运行
						Type inf(info);
						threadTask<Type> addtask;
						
						addtask.setTask(inf, std::get<0>(taskList[info.task_id]));
						pool->taskPush(addtask);
						printf("already Add task id[%d]\n", info.task_id);

						// 标记任务在运行
						std::get<1>(taskList[info.task_id]) = true;
					}
				} 
			}
		}	

	public:
		// 待测试任务
		void fun1(Type data) {		// 占用线程池线程大概1s
			printf("task1: %f thread: %ld pid: %d\n", data.s_end, pthread_self(), getpid());
			printf("fun1 start exec...\n");
			sleep(1);
			printf("fun1 exec finish...\n");

			// reset flag
			resetTaskListFlag(data);
		}

		void fun2(Type data) {		// 占用线程池线程大概2s
			printf("task2: %f thread: %ld pid: %d\n", data.s_end, pthread_self(), getpid());
			printf("fun2 start exec...\n");
			sleep(2);
			printf("fun2 exec finish...\n");

			// reset flag
			resetTaskListFlag(data);
		}

		void fun3(Type data) {   	// 占用线程池线程大概3s
			printf("task3: %f thread: %ld pid: %d\n", data.s_end, pthread_self(), getpid());
			printf("fun3 start exec...\n");
			sleep(3);
			printf("fun3 exec finish...\n");

			// reset flag
			resetTaskListFlag(data);
		}
	
	private:
		std::shared_ptr<threadPools<Type>>  pool;		// 线程池

		// 任务函数注册
		// 任务id, tuple(任务函数, 任务是否锁定（正在执行就将任务锁定，避免重复进线程））
		// 注意: [可以使用匿名函数绑定this指针，也可以使用bind函数绑定this指针]
		std::map< int, std::tuple<std::function<void(Type )>, bool> >  taskList = {
			// {1, std::make_tuple( std::bind(&ros1Task::fun1, this, std::placeholders::_1), false ) },
			{1, std::make_tuple( [this](Type param) { this->fun1(param); }, false ) },
			{2, std::make_tuple( [this](Type param) { this->fun2(param); }, false ) },
			{3, std::make_tuple( [this](Type param) { this->fun3(param); }, false ) }
		};

		// 任务执行信息表（条件）
		std::vector<Type> taskListInfo = {
			{1, false, 0, 10},
			{2, false, 0, 20},
			{3, false, 0, 30}
		};

		// reset flag
		inline void resetTaskListFlag(Type param) {
			std::get<1>(taskList[param.task_id]) = false;
		}
};

int main(int argc, char** argv) {

    ros::init(argc, argv, "TaskNode");
	std::cout << "-----based on [product-costom model for thread pools]-----main thread pid: " << getpid() << std::endl;

	std::shared_ptr<ros1Task> taskNode = std::make_shared<ros1Task>(2, 5);

    ros::Rate r(0.2);
	static float i=0;
	
	while (ros::ok()) {		

		std_msgs::Float32 taskInfo;
		taskInfo.data = i++;
		taskNode->taskInfo_pub.publish(taskInfo);

		ros::spinOnce();	// 触发回调
		r.sleep();
	}

	return 0;
}














