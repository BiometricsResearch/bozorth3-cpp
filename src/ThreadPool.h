#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool
{
private:
	std::vector<std::thread> workers_;
	std::queue<std::function<void()>> tasks_;
	std::mutex queue_mutex_;
	std::condition_variable condition_;
	bool should_stop_;

	std::mutex busy_workers_mutex_;
	std::atomic<std::size_t> busy_workers_ = 0;
	std::condition_variable workers_done_;

public:
	explicit ThreadPool(size_t threads)
		: should_stop_{false}
	{
		for (size_t i = 0; i < threads; ++i)
			workers_.emplace_back(
				[this]
				{
					for (;;)
					{
						std::function<void()> task;
						{
							std::unique_lock<std::mutex> lock{queue_mutex_};
							condition_.wait(lock, [this]
							{
								return should_stop_ || !tasks_.empty();
							});

							if (should_stop_ && tasks_.empty())
							{
								return;
							}

							task = std::move(tasks_.front());
							tasks_.pop();
						}

						{
							std::lock_guard lg{busy_workers_mutex_};
							busy_workers_.fetch_add(1);
						}
						workers_done_.notify_all();

						task();

						{
							std::lock_guard lg{busy_workers_mutex_};
							busy_workers_.fetch_sub(1);
						}
						workers_done_.notify_all();
					}
				});
	}


	template <class F, class... Args>
	std::future<typename std::invoke_result<F, Args...>::type> enqueue(F&& f, Args&&... args)
	{
		using ReturnType = typename std::invoke_result<F, Args...>::type;
		auto task = std::make_shared<std::packaged_task<ReturnType()>>(
			std::bind(std::forward<F>(f), std::forward<Args>(args)...));
		std::future<ReturnType> result = task->get_future();
		{
			std::unique_lock<std::mutex> lock{queue_mutex_};
			if (should_stop_)
			{
				throw std::runtime_error("enqueue on stopped ThreadPool");
			}
			tasks_.emplace([task]() { (*task)(); });
		}
		condition_.notify_one();
		return result;
	}

	void drain()
	{
		{
			std::lock_guard lock{queue_mutex_};
			while (!tasks_.empty())
			{
				tasks_.pop();
			}
		}

		std::unique_lock lock{busy_workers_mutex_};
		workers_done_.wait(lock, [&]()
		{
			return busy_workers_.load() == 0;
		});
	}

	~ThreadPool()
	{
		{
			std::unique_lock<std::mutex> lock{queue_mutex_};
			should_stop_ = true;
		}
		condition_.notify_all();
		for (std::thread& worker : workers_)
		{
			worker.join();
		}
	}
};

#endif
