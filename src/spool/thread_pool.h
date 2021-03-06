#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <memory>
#include <ranges>
#include <array>
#include <type_traits>
#include <cassert>

#include "concepts.h"
#include "wsq.h"
#include "MPMCQueue.h"
#include "job.h"
#include "job_utils.h"
#include "input_data.h"

#ifndef __cpp_lib_ranges
#error "Spool requires a complete (or near complete) ranges implementation, check your compiler settings"
#endif // !__cpp_lib_ranges

namespace spool
{

	constexpr size_t max_unassigned_jobs = 2056;
	constexpr size_t max_assigned_jobs = 1024;

	namespace detail
	{
		struct thread_context final
		{
			thread_pool* pool;
			size_t runner_index;
		};
	}

	struct execution_context final
	{
		thread_pool* pool;
		std::shared_ptr<job> active_job;
	};

	template<typename T>
	struct [[nodiscard]] data_job final
	{
		std::shared_ptr<job> job;
		std::shared_ptr<input_data<T>> data;
	};
	
	enum class attach_result
	{
		attached_and_ran, already_worker, max_already_attached
	};

	class thread_pool final
	{
	public:

		thread_pool(unsigned int thread_count = std::thread::hardware_concurrency(), unsigned int attachable_workers = 0)
			:unassigned_jobs(max_unassigned_jobs),
			unattached_workers(attachable_workers)
		{
			assert(thread_count >= 0);
			assert(attachable_workers >= 0);
			assert(thread_count + attachable_workers > 0);
			for (unsigned int i = 0; i < thread_count + attachable_workers; i++)
			{
				workers.emplace_back(i);
			}
			for (unsigned int i = 0; i < thread_count; i++)
			{
				child_threads.emplace_back(run_worker, this, i);
			}
		}

		~thread_pool()
		{
			//let all our child threads finish up
			wait_exit();
		}

		//moving or copying breaks so much, so we simply won't permit it
		thread_pool(const thread_pool& other) = delete;
		thread_pool(thread_pool&& other) = delete;

		attach_result attach_as_worker()
		{
			if(get_execution_context().pool != nullptr)
			{
				return attach_result::already_worker;
			}
			const auto attachment = unattached_workers.fetch_sub(1);
			if(attachment <= 0)
			{
				return attach_result::max_already_attached;
			}
			workers.at(workers.size() - attachment).run(this);
			thread_pool::context.pool = nullptr;
			return attach_result::attached_and_ran;
		}

#pragma region base_job

		template<job_func F>
		std::shared_ptr<job> enqueue_job(F&& work)
		{
			const std::shared_ptr<job> pjob(new job(std::forward<std::function<void()>>(work)));
			enqueue_job(pjob);
			return pjob;
		}

		template<job_func F, usable_prerequisite P>
		std::shared_ptr<job> enqueue_job(F&& work, P&& prerequisite)
		{
			const std::shared_ptr<job> pjob(new job(std::forward<std::function<void()>>(work), std::forward<P>(prerequisite)));
			enqueue_job(pjob);
			return pjob;
		}

#pragma endregion base_job

#pragma region data_job

		template<typename T, typename F>
			requires std::invocable<F, T&>
		data_job<T> enqueue_data_job(F&& work)
		{
			std::shared_ptr<input_data<T>> data = std::make_shared<input_data<T>>();
			auto job = enqueue_shared_resource_job(std::forward<F>(work), read_provider<T, input_data<T>, std::shared_ptr<input_data<T>>>(data));
			return {job, data};
		}
		
		template<typename T, typename F, usable_prerequisite P>
			requires std::invocable<F, T&>
		data_job<T> enqueue_data_job(F&& work, P&& prerequisite)
		{
			std::shared_ptr<input_data<T>> data = std::make_shared<input_data<T>>();
			auto job = enqueue_shared_resource_job(std::forward<F>(work), read_provider<T, input_data<T>, std::shared_ptr<input_data<T>>>(data));
			return { job, data };
		}
		
#pragma endregion data_job

#pragma region shared_resource_job

		template<typename F, typename ... Ps>
			requires std::invocable<F, provider_underlying_type<Ps>& ...>
		std::shared_ptr<job> enqueue_shared_resource_job(F&& func, Ps&& ... providers)
		{
			return enqueue_job(detail::create_shared_resource_job_func<F, Ps ...>(std::forward<F>(func), std::forward<Ps>(providers)...));
		}

		template<typename F, typename ... Ps, usable_prerequisite Pr>
			requires std::invocable<F, provider_underlying_type<Ps>& ...>
		std::shared_ptr<job> enqueue_shared_resource_job(F&& func, Pr&& prerequisite, Ps&& ... providers)
		{
			return enqueue_job(detail::create_shared_resource_job_func<F, Ps ...>(std::forward<F>(func), std::forward<Ps>(providers)...), std::forward<Pr>(prerequisite));
		}
		
#pragma region impl_helpers
		template<std::ranges::forward_range R, std::copy_constructible F>
			requires std::invocable<F, range_underlying<R>&>
		std::vector<std::shared_ptr<job>> for_each(R& range, const F& work)
		{
			const auto chunks = detail::split_range(range, workers.size());
			std::vector<std::shared_ptr<job>> jobs;
			std::ranges::for_each(chunks, [&](auto& chunk) {jobs.emplace_back(enqueue_job([=]() {std::ranges::for_each(chunk, work); })); });
			return jobs;

		}

		template<std::ranges::forward_range R, std::copy_constructible F, usable_prerequisite P>
			requires std::invocable<F, range_underlying<R>&>
		std::vector<std::shared_ptr<job>> for_each(R& range, const P& prerequisite, const F& work)
		{
			const auto chunks = detail::split_range(range, workers.size());
			std::vector<std::shared_ptr<job>> jobs;
			std::ranges::for_each(chunks, [&](auto& chunk) {jobs.emplace_back(enqueue_job([=]() {std::ranges::for_each(chunk, work); }, prerequisite)); });
			return jobs;
		}
#pragma endregion impl_helpers


		static execution_context get_execution_context()
        {
            if (thread_pool::context.pool != nullptr)
            {
                return { thread_pool::context.pool, thread_pool::context.pool->workers[thread_pool::context.runner_index].active_job };
            }
            else return { nullptr, nullptr };
        }

		//prevent new tasks from being started by the thread pool
		void exit()
		{
			exiting.test_and_set();
		}

		//tells the thread pool to not start new jobs, and then block the calling thread until all worker threads are finished, indicating the pool can be safely destroyed, returns false if called from a worker thread and could not guarentee full wait cleanup
		bool wait_exit()
		{
			exit();
			for (auto& thread : child_threads)
			{
				if(thread.get_id() != std::this_thread::get_id() && thread.joinable())
				{
					thread.join();
				}
			}
			workers.clear();
			return thread_pool::context.pool != this;
		}

	private:
		struct worker
		{
			worker(int index)
				:work_queue(max_assigned_jobs),
				active_job(nullptr),
				worker_index(index)
			{}

			detail::WorkStealingQueue<std::shared_ptr<job>> work_queue;
			std::shared_ptr<job> active_job;
			size_t worker_index;

			void run(thread_pool* pool)
            {
                std::deque<std::shared_ptr<job>> held_jobs;
                thread_pool::context = { pool, worker_index };
                while (!pool->exiting.test())
                {
                    active_job = pool->next_job(worker_index);
                    if (active_job != nullptr)
                    {
                        //we actually have a job, run it
                        if (active_job->try_run())
                        {
                            //job completed succesfully, offer to delete then dump all our held jobs back into the queue
                            active_job = nullptr;

                            while (!held_jobs.empty())
                            {
                                pool->workers[worker_index].work_queue.push(held_jobs.back());
                                held_jobs.pop_back();
                            }
                        }
                        else
                        {
                            //the job couldn't run, hold it
                            held_jobs.push_back(active_job);
                        }
                    }
                    else
                    {
                        //no job offered, dump our held jobs back
                        while (!held_jobs.empty())
                        {
                            pool->workers[worker_index].work_queue.push(held_jobs.back());
                            held_jobs.pop_back();
                        }
                    }
                }
            }

		};

		struct additionalWorker
		{

		};

		std::shared_ptr<job> next_job(size_t worker_index)
		{
			const std::optional<std::shared_ptr<job>> immediate_job = workers[worker_index].work_queue.pop();
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

			//try to steal from other queues, going "right"
			size_t steal_index = worker_index;
			do
            {
				steal_index++;
				//if we've wrapped around, reset back
				if (steal_index >= workers.size()) steal_index = 0;
				std::optional<std::shared_ptr<job>> stolen_job = workers[steal_index].work_queue.steal();
				if (stolen_job.has_value()) return stolen_job.value();
			}
			while(steal_index != worker_index);
			return nullptr;
		}

		void enqueue_job(const std::shared_ptr<job>& new_job)
		{
			if (context.pool == this)
			{
				workers[context.runner_index].work_queue.push(new_job);
			}
			else
			{
				unassigned_jobs.emplace(new_job);
			}
		}

		static void run_worker(thread_pool* pool, size_t worker_index)
		{
			pool->workers[worker_index].run(pool);
		}
		
		rigtorp::mpmc::Queue<std::shared_ptr<job>> unassigned_jobs;
		std::atomic_int unattached_workers;
		std::deque<worker> workers;
		std::deque<std::thread> child_threads;
		std::atomic_flag exiting;

		inline static thread_local detail::thread_context context = { nullptr, SIZE_MAX };
	};

}
