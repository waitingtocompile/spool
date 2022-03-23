#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <array>

#include "wsq.h"
#include "MPMCQueue.h"
#include "job.h"

namespace spool
{
	shared_semaphore make_shared_semaphore()
	{
		return std::make_shared<std::atomic_flag>();
	}

	class thread_pool
	{
	public:
		thread_pool(unsigned int thread_count = std::thread::hardware_concurrency(), size_t max_unassigned_jobs = 2056, size_t max_assigned_jobs = 1024)
			:unassigned_jobs(max_unassigned_jobs)
		{
			for (int i = 0; i < thread_count; i++)
			{
				worker_threads.emplace_back(max_unassigned_jobs, this, i);
			}
		}

		~thread_pool()
		{
			wait_exit();
		}

		//some of our container types cannot be moved or copied, and moving or copying would break the look-back on workers, so the thread pool can't either
		thread_pool(const thread_pool& other) = delete;
		thread_pool(thread_pool&& other) = delete;

		template<typename... Args>
			requires std::constructible_from<spool::job, Args...>
		job& enqueue_job(Args... params)
		{
			static_assert(sizeof...(Args) > 0, "Cannot enqueue a job with no parameters");
			std::unique_ptr<job> new_job = std::make_unique<job>(params...);
			job& ref = *new_job;
			unassigned_jobs.emplace(std::move(new_job));
			return ref;
		}

		/*

		job& enqueue_job(std::function<void()> work)
		{
			//for now put it in the unassigned pile
			std::unique_ptr<job> pjob= std::make_unique<job>(work);
			job& ptr = *pjob;
			unassigned_jobs.emplace(std::move(pjob));
			return ptr;
		}
		template<typename R>
			requires std::ranges::input_range<R>&& std::convertible_to<std::iter_value_t<std::ranges::iterator_t<R>>, shared_semaphore>
		job& enqueue_job(std::function<void()> work, R prerequisites)
		{
			//for now put it in the unassigned pile
			std::unique_ptr<job> pjob = std::make_unique<job>(work, prerequisites);
			job* ptr = pjob.get();
			unassigned_jobs.emplace(std::move(pjob));
			return *ptr;
		}
		job& enqueue_job(std::function<void()> work, shared_semaphore prerequisite)
		{
			
		}

		*/

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
		}

	private:
		struct worker
		{
			worker(size_t queue_size, thread_pool* pool, int worker_index)
				:work_queue(queue_size), thread(run_worker, pool, worker_index)
			{}

			detail::WorkStealingQueue<std::unique_ptr<job>> work_queue;
			std::thread thread;

			
		};

		std::unique_ptr<job> request_additional_job(int worker_index)
		{
			//start by trying unassigned jobs
			std::unique_ptr<job> assigned_job;
			if (unassigned_jobs.try_pop(assigned_job))
			{
				return assigned_job;
			}
			//try to steal from other queues, going "left"
			int steal_index = worker_index - 1;
			while (steal_index != worker_index)
			{
				//if we've wrapped around, reset back
				if (steal_index < 0) steal_index = worker_threads.size() - 1;
				std::optional<std::unique_ptr<job>> stolen_job = worker_threads[steal_index].work_queue.steal();
				if (stolen_job.has_value()) return std::move(stolen_job.value());
				steal_index--;
			}
			return nullptr;
		}

		static void run_worker(thread_pool* pool, int worker_index)
		{
			detail::WorkStealingQueue<std::unique_ptr<job>>* queue = &(pool->worker_threads[worker_index].work_queue);

			while (!pool->exiting.test())
			{
				//pop jobs off our local queue until we can't any more
				std::optional<std::unique_ptr<job>> found_job = queue->pop();
				std::deque<job> held_jobs;
				while (!pool->exiting.test() && found_job.has_value())
				{
					if (found_job.value()->try_run())
					{
						//a job finished, re-emplace all the held jobs
						while (!held_jobs.empty())
						{
							queue->push(std::move(held_jobs.back()));
							held_jobs.pop_back();
						}
					}
					else
					{
						//the job couldn't run, hold it and grab another
						held_jobs.emplace_back(std::move(found_job.value()));
					}

					found_job = queue->pop();
				}
				//we have nothing in the queue that can be run, try to grab unassigned jobs
				//first we re-emplace all the held jobs
				while (!held_jobs.empty())
				{
					queue->push(std::move(held_jobs.back()));
					held_jobs.pop_back();
				}

				std::unique_ptr<job> additional_job = pool->request_additional_job(worker_index);
				bool ran_sucessfully = false;
				while (!pool->exiting.test() && additional_job != nullptr && !ran_sucessfully)
				{
					if (additional_job->try_run())
					{
						//a job finished, re-emplace all the held jobs
						while (!held_jobs.empty())
						{
							queue->push(std::move(held_jobs.back()));
							held_jobs.pop_back();
						}
						ran_sucessfully = true;
					}
					else
					{
						//the job couldn't run, hold it and grab another
						held_jobs.emplace_back(std::move(found_job.value()));
						additional_job = pool->request_additional_job(worker_index);
					}
				}
				
				//go back to checking our own queue
			}
		}
		
		rigtorp::mpmc::Queue<std::unique_ptr<job>> unassigned_jobs;
		std::vector<worker> worker_threads;
		std::atomic_flag exiting;
	};
}