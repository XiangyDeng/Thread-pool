#ifndef ILOVERS_THREAD_POOL_H
#define ILOVERS_THREAD_POOL_H

#include <iostream>
#include <functional> // std::function()
#include <string>
#include <thread>
#include <condition_variable>
#include <future>
#include <atomic>
#include <vector>
#include <queue>

using namespace std;

// 命名空间
namespace ilovers {
	class TaskExecutor;
}

class ilovers::TaskExecutor {
	// 起别名：效果等同于typedef,std::function()函数封装，形参与返回值为void
	using Task = std::function<void()>;
private:
	// 线程池
	std::vector<std::thread> pool;
	// 任务队列
	std::queue<Task> tasks;
	// 同步
	std::mutex m_task;
	std::condition_variable cv_task;
	// 是否关闭提交
	std::atomic<bool> stop;

public:
	// 构造
	TaskExecutor(size_t size = 4) : stop{ false } {
		size = size < 1 ? 1 : size;
		// 构造线程池
		for (size_t i = 0; i < size; ++i) {
			// <void(TaskExecutor::*)(),TaskExecutor*>
			pool.emplace_back(&TaskExecutor::schedual, this);    // push_back(std::thread(...))
		}
	}

	// 析构
	~TaskExecutor() {
		for (std::thread& thread : pool) {
			thread.detach();    // 让线程“自生自灭”
			//thread.join();        // 等待任务结束， 前提：线程一定会执行完
		}
	}

	// 停止任务提交
	void shutdown() {
		this->stop.store(true);
	}

	// 重启任务提交
	void restart() {
		this->stop.store(false);	// std::atomic<bool> 的store()函数：原子地存储原子对象的值
	}

	// 提交一个任务
	// 可变参数模板：class... Args
	template<class F, class... Args>
	// 右值引用
	// std::future提供了一种用于访问异步操作结果的机制。
	// std::future所引用的共享状态不能与任何其它异步返回的对象共享(与std::shared_future相反)
	// ->std::future<decltype(f(args...))> 可去掉？？？？？？？？？？？
	auto commit(F&& f, Args&&... args) ->std::future<decltype(f(args...))> {
		if (stop.load()) {    // stop == true :停止task commit。 std::atomic<bool> 的load()函数：原子地获得存储于原子对象的值
			throw std::runtime_error("task executor have closed commit.");					
		}

		using ResType = decltype(f(args...));    // typename std::result_of<F(Args...)>::type, 函数 f 的返回值类型
		auto task = std::make_shared<std::packaged_task<ResType()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...)
			);    // wtf !
		{    // 添加任务到队列
			std::lock_guard<std::mutex> lock{ m_task }; // 上锁
			tasks.emplace([task]() {                 // push(Task{...})，lambda表达式
				cout << "Add task in thread( " << std::this_thread::get_id() << ")" << endl;
				(*task)();
			});
		}
		cv_task.notify_all();    // 唤醒线程执行

		// 获取任务的future对象（未来值），用于在主线程获取目标值
		std::future<ResType> future = task->get_future();
		return future;
	}

private:
	// 获取一个待执行的 task，Task定义为std::function()
	Task get_one_task() {
		std::unique_lock<std::mutex> lock{ m_task };
		// 1.std::condition_variable:使用条件变量（condition_variable）实现多个线程间的同步操作；
		// 当条件不满足时，相关线程被一直阻塞，直到某种条件出现，这些线程才会被唤醒。
		// 2.lambda表达式：捕获 ilovers::TaskExecutor*，返回tasks成员是否为空
		cv_task.wait(lock, [this]()->bool{ return !tasks.empty(); });    // wait 直到有 task， 在commit函数中提交任务

		Task task{ std::move(tasks.front()) };    // 右值移动取一个 task
		tasks.pop();
		return task;
	}

	// 任务调度，在子线程中调度
	void schedual() {
		//cout << "id:" << std::this_thread::get_id() << endl;
		while (true) {
			// task为std::function()
			if (Task task = get_one_task()) {
				task();    //
			}
			else {
				
			}
		}
	}
};

#endif

void f()
{
	std::cout << "hello, f !" << std::endl;
}

struct G {
	int operator()() {
		std::cout << "hello, g !" << std::endl;
		return 42;
	}
};

int main()
try {
	// 创建10个线程
	ilovers::TaskExecutor executor{ 10 };

	// 三种commit函数的调用方式:普通函数、仿函数、匿名函数
	// auto commit(F&& f, Args&&... args)-> std::future<decltype(f(args...))>{}
	std::future<void> ff = executor.commit(f);
	std::future<int> fg = executor.commit(G());
	std::future<std::string> fh = executor.commit([]()->std::string { std::cout << "hello, h !" << std::endl; return "hello,fh !"; });

	// 重启任务 atomic<bool> 为false
	executor.shutdown();

	ff.get();
	cout << "fg get(): " << fg.get() << endl;
	cout << "fh get(): " << fh.get() << endl;

	std::this_thread::sleep_for(std::chrono::seconds(1));
	executor.restart();    // 重启任务 atomic<bool> 为true
	executor.commit(f).get();    

	std::cout << "end..." << std::endl;
	return 0;
}
catch (std::exception& e) {
	std::cout << "some unhappy happened... " << e.what() << std::endl;
}
