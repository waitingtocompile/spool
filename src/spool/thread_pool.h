#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <memory>

#include "wsq.h"
#include "MPMCQueue.h"
#include "job.h"

namespace spool
{
	constexpr size_t max_unassigned_jobs = 2056;
	constexpr size_t max_assigned_jobs = 1024;

	class thread_pool
	{
	public:
		thread_pool(unsigned int thread_count = std::thread::hardware_concurrency())
			:unassigned_jobs(max_unassigned_jobs)
		{
			for (int i = 0; i < thread_count; i++)
			{
				worker_threads.emplace_back();
			}
			for (int i = 0; i < thread_count; i++)
			{
				worker_threads[i].thread = std::thread(run_worker, this, i);
			}
		}

		~thread_pool()
		{
			wait_exit();

			//local queues are handled by their deconstructors
		}

		//moving or copying breaks so much, so we simply won't permit it
		thread_pool(const thread_pool& other) = delete;
		thread_pool(thread_pool&& other) = delete;

		std::shared_ptr<job> enqueue_job(std::function<void()> work)
		{
			//we're breaking the no-new rule for shared stuff, since the constructors are private
			std::shared_ptr<job> pjob(new job(work));
			unassigned_jobs.emplace(pjob);
			return pjob;
		}
		template<typename R>
			requires std::ranges::input_range<R>&& std::convertible_to<std::iter_value_t<std::ranges::iterator_t<R>>, std::shared_ptr<job>>
		std::shared_ptr<job> enqueue_job(std::function<void()> work, R prerequisites)
		{
			//for now put it in the unassigned pile
			std::shared_ptr<job> pjob(new job(work, prerequisites));
			unassigned_jobs.emplace(pjob);
			return pjob;
		}

		std::shared_ptr<job> enqueue_job(std::function<void()> work, std::shared_ptr<job> prerequisite)
		{
			std::shared_ptr<job> pjob(new job(work));
			pjob->add_prerequisite(prerequisite);
			unassigned_jobs.emplace(pjob);
			return pjob;
		}

		//prevent new tasks from being started by the thread pool
		void exit()
		{
			exiting.test_and_set();
		}

		//tells the thread pool to not start new jobs, and then block the calling thread until all worker threads are finished, indicating the pool can be safely destroyed
		void wait_exit()
		{
			exit();
			for (auto& worker : worker_threads)
			{
				worker.thread.join();
			}
			worker_threads.clear();
		}

	private:
		struct worker
		{
			worker()
				:work_queue(max_assigned_jobs)
			{}

			detail::WorkStealingQueue<std::shared_ptr<job>> work_queue;
			std::thread thread;

			~worker()
			{

			}
		};

		std::shared_ptr<job> next_job(int worker_index)
		{
			std::optional<std::shared_ptr<job>> immediate_job = worker_threads[worker_index].work_queue.pop();
			if (immediate_job.has_value())
			{
				return immediate_job.value();
			}

			//no job on own queue, try to pull from unassigned queue
			std::shared_ptr<job> assigned_job = nullptr;
			if (unassigned_jobs.try_pop(assigned_job))
			{
				//job poppped off unassigned queue, use that
				return assigned_job;
			}

			//try to steal from other queues, going "left"
			int steal_index = worker_index;
			while (steal_index != worker_index)
			{
				steal_index--;
				//if we've wrapped around, reset back
				if (steal_index < 0) steal_index = worker_threads.size() - 1;
				std::optional<std::shared_ptr<job>> stolen_job = worker_threads[steal_index].work_queue.steal();
				if (stolen_job.has_value()) return stolen_job.value();
			}
			return nullptr;
		}

		static void run_worker(thread_pool* pool, int worker_index)
		{
			std::deque<std::shared_ptr<job>> held_jobs;

			while (!pool->exiting.test())
			{
				std::shared_ptr<job> next = pool->next_job(worker_index);
				if (next != nullptr)
				{
					//we actually have a job, run it
					if (next->try_run())
					{
						//job completed succesfully, offer to delete then dump all our held jobs back into the queue
						next = nullptr;

						while (!held_jobs.empty())
						{
							pool->worker_threads[worker_index].work_queue.push(held_jobs.back());
							held_jobs.pop_back();
						}
					}
					else
					{
						//the job couldn't run, hold it
						held_jobs.push_back(next);
					}
				}
				else
				{
					//no job offered, dump our held jobs back
					while (!held_jobs.empty())
					{
						pool->worker_threads[worker_index].work_queue.push(held_jobs.back());
						held_jobs.pop_back();
					}
				}
			}
		}
		
		rigtorp::mpmc::Queue<std::shared_ptr<job>> unassigned_jobs;
		std::deque<worker> worker_threads;
		std::atomic_flag exiting;
	};
}