#pragma once
#include <thread>
#include <vector>

#include "wsq.h"
#include "MPMCQueue.h"
#include "job.h"

namespace spool
{

	class thread_pool
	{
	public:
		thread_pool(unsigned int thread_count = std::thread::hardware_concurrency(), size_t max_unassigned_jobs = 2056, size_t max_assigned_jobs = 1024);
		//some of our contianer types cannot be moved or copied, so the thread pool can't either
		thread_pool(const thread_pool& other) = delete;
		thread_pool(thread_pool&& other) = delete;
		



	private:
		class worker
		{
			detail::WorkStealingQueue<job> work_queue;
			std::thread thread;
		};

		std::vector<worker> worker_threads;
		rigtorp::mpmc::Queue<job> unassigned_jobs;
	};
}