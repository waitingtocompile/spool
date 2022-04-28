#pragma once
#include <thread>
#include <vector>
#include <deque>
#include <memory>
#include <ranges>
#include <array>
#include <type_traits>

#include "concepts.h"
#include "wsq.h"
#include "MPMCQueue.h"
#include "job.h"
#include "job_data.h"

#ifndef __cpp_lib_ranges
#error "Spool requires a complete (or near complete) ranges implementation, check your compiler settings"
#endif // !__cpp_lib_ranges

namespace spool
{

	constexpr size_t max_unassigned_jobs = 2056;
	constexpr size_t max_assigned_jobs = 1024;

	namespace detail
	{
		struct thread_context
		{
			thread_pool* pool;
			int runner_index;
		};

		template<std::ranges::forward_range R>
		std::vector<std::ranges::take_view<std::ranges::drop_view<std::ranges::ref_view<R>>>> split_range(R& range, size_t max_chunks)
		{
			std::vector<std::ranges::take_view<std::ranges::drop_view<std::ranges::ref_view<R>>>> views;

			//if there's more chunks than elements in the range, return one chunk per element
			if (max_chunks >= std::ranges::size(range))
			{
				for (int i = 0; i < std::ranges::size(range); i++)
				{
					 views.emplace_back(range | std::views::drop(i) | std::views::take(1));
				}
			}
			else
			{
				//subdivide as normal
				size_t chunk_size = std::ranges::size(range) / max_chunks;
				size_t chunk_extra = std::ranges::size(range) % max_chunks;
				
				size_t step = 0;
				for (size_t i = 0; i < max_chunks; i++)
				{
					size_t chunk = chunk_size + ((i < chunk_extra) ? 1 : 0);
					views.emplace_back(range | std::views::drop(step) | std::views::take(chunk));
					step += chunk;
				}
			}
			return views;
		}
	}

	struct execution_context
	{
		thread_pool* const pool;
		const std::shared_ptr<job> active_job;
	};

	template<typename T>
	struct data_job
	{
		std::shared_ptr<job> job;
		std::shared_ptr<job_data<T>> data_handle;
	};

	class thread_pool
	{
	public:

		thread_pool(unsigned int thread_count = std::thread::hardware_concurrency())
			:unassigned_jobs(max_unassigned_jobs)
		{
			assert(thread_count > 0);
			for (int i = 0; i < thread_count; i++)
			{
				worker_threads.emplace_back(i);
			}
			for (int i = 0; i < thread_count; i++)
			{
				worker_threads[i].thread = std::thread(run_worker, this, i);
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

#pragma region base_job
		//TODO: consider moving these to use std::invocable like the other enqueue functions, would require wrapping and a specialisation for std::functions

		template<typename F>
			requires std::convertible_to<F, std::function<void()>>
		std::shared_ptr<job> enqueue_job(F&& work)
		{
			//we're breaking the no-new rule for shared stuff, since the constructors are private
			std::shared_ptr<job> pjob(new job(std::forward<F>(work)));
			enqueue_job(pjob);
			return pjob;
		}

		template<typename F, prerequisite_range R>
			requires std::convertible_to<F, std::function<void()>>
		std::shared_ptr<job> enqueue_job(F&& work, const R& prerequisites)
		{
			//for now put it in the unassigned pile
			std::shared_ptr<job> pjob(new job(std::forward<F>(work), prerequisites));
			enqueue_job(pjob);
			return pjob;
		}

		template<typename F>
			requires std::convertible_to<F, std::function<void()>>
		std::shared_ptr<job> enqueue_job(F&& work, std::shared_ptr<detail::prerequisite_base> prerequisite)
		{
			std::shared_ptr<job> pjob(new job(std::forward<F>(work)));
			pjob->add_prerequisite(prerequisite);
			enqueue_job(pjob);
			return pjob;
		}

#pragma endregion base_job

#pragma region data_job
		
		template<typename T, typename F>
			requires std::invocable<F, T&>
		data_job<T> enqueue_job(F&& work)
		{
			return enqueue_job(std::forward<F>(work), std::make_shared<job_data<T>>());
		}

		template<typename T, typename F>
			requires std::invocable<F, T&>
		data_job<T> enqueue_job(F&& work, std::shared_ptr<job_data<T>> data)
		{
			return { enqueue_job(create_data_job_func(std::forward<F>(work), data), data), std::move(data)};
		}
		
		template<typename T, typename F>
			requires std::invocable<F, T&>
		data_job<T> enqueue_job(F&& work, std::shared_ptr<detail::prerequisite_base> prerequisite)
		{
			return enqueue_job(std::forward<F>(work), std::make_shared<job_data<T>>(), prerequisite);
		}

		template<typename T, typename F>
			requires std::invocable<F, T&>
		data_job<T> enqueue_job(F&& work, std::shared_ptr<job_data<T>> data, std::shared_ptr<detail::prerequisite_base> prerequisite)
		{
			std::array<std::shared_ptr<detail::prerequisite_base>, 2> prereqs{ data, prerequisite };
			return { enqueue_job(create_data_job_func(std::forward<F>(work), data), prereqs), std::move(data) };
		}
		
		template<typename T, typename F, prerequisite_range R>
			requires std::invocable<F, T&>
		data_job<T> enqueue_job(F&& work, const R& prerequisites)
		{
			return enqueue_job(std::forward(work), prerequisites);
		}

		template<typename T, typename F, prerequisite_range R>
			requires std::invocable<F, T&>
		data_job<T> enqueue_job(F&& work, std::shared_ptr<job_data<T>> data, const R& prerequisites)
		{
			std::shared_ptr<job> pjob(new job(create_data_job_func(std::forward<F>(work), data), prerequisites));
			pjob->add_prerequisite(data);
			enqueue_job(pjob);
			return (std::move(pjob), std::move(data));
		}
		
#pragma endregion data_job

#pragma region shared_resource_job

		template<typename F, typename P, typename ... Ps>
			requires std::invocable<F, provider_underlying_type<P>&, provider_underlying_type<Ps>& ...>
		std::shared_ptr<job> enqueue_job(F&& func, P& provider, Ps& ... providers)
		{
			return enqueue_job(create_data_job_func<F, P, Ps ...>(std::forward<F>(func), provider, providers...));
		}

		template<typename F, typename P, typename ... Ps, prerequisite_range R>
			requires std::invocable<F, provider_underlying_type<P>&, provider_underlying_type<Ps>& ...>
		std::shared_ptr<job> enqueue_job(F&& func, P& provider, Ps& ... providers, const R& prerequisites)
		{
			return enqueue_job(create_data_job_func<F, P, Ps ...>(std::forward<F>(func), provider, providers...), prerequisites);
		}

		template<typename F, typename P, typename ... Ps>
			requires std::invocable<F, provider_underlying_type<P>&, provider_underlying_type<Ps>& ...>
		std::shared_ptr<job> enqueue_job(F&& func, P& provider, Ps& ... providers, std::shared_ptr<detail::prerequisite_base> prerequisite)
		{
			return enqueue_job(create_data_job_func<F, P ...>(std::forward<F>(func), provider, providers...), std::move(prerequisite));
		}
		
#pragma region impl_helpers
		template<std::ranges::forward_range R, std::copy_constructible F>
			requires std::invocable<F, range_underlying<R>&>
		std::vector<std::shared_ptr<job>> for_each(R& range, const F& work)
		{
			std::vector<std::shared_ptr<job>> jobs;
			for (const auto& chunk : detail::split_range(range, worker_threads.size()))
			{
				jobs.emplace_back(enqueue_job([=]() {
					for (auto& elem : chunk)
					{
						work(elem);
					}
				}));
			}
			return jobs;
		}

		template<std::ranges::forward_range R, std::copy_constructible F, job_range J>
			requires std::invocable<F, range_underlying<R>&>
		std::vector<std::shared_ptr<job>> for_each(R& range, const F& work, J prerequisites)
		{
			std::vector<std::shared_ptr<job>> jobs;
			for (const auto& chunk : detail::split_range(range, worker_threads.size()))
			{
				jobs.emplace_back(enqueue_job([=]() {
					for (auto& elem : chunk)
					{
						work(elem);
					}
				}), prerequisites);
			}
			return jobs;
		}

		template<std::ranges::forward_range R, std::copy_constructible F>
			requires std::invocable<F, range_underlying<R>&>
		std::vector<std::shared_ptr<job>> for_each(R& range, const F& work, std::shared_ptr<job> prerequisite)
		{
			std::vector<std::shared_ptr<job>> jobs;
			for (const auto& chunk : detail::split_range(range, worker_threads.size()))
			{
				jobs.emplace_back(enqueue_job([=]() {
					for (auto elem : chunk)
					{
						work(elem);
					}
				}), prerequisite);
			}
			return jobs;
		}
#pragma endregion impl_helpers


		static execution_context get_execution_context();


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
			worker(int index)
				:work_queue(max_assigned_jobs),
				active_job(nullptr),
				worker_index(index)
			{}

			detail::WorkStealingQueue<std::shared_ptr<job>> work_queue;
			std::thread thread;
			std::shared_ptr<job> active_job;
			int worker_index;

			void run(thread_pool* pool);
		};

		template<typename T>
		static T& extract(const std::shared_ptr<job_data<T>>& data)
		{
			return data->data;
		}

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

		template<std::ranges::forward_range R>
		std::vector<std::shared_ptr<job>> create_jobs(R& range, std::function<void(range_underlying<R>&)> work)
		{
			std::vector<std::shared_ptr<job>> jobs;
			int stride = worker_threads.size();
			for (int offset = 0; offset < stride; offset++)
			{
				jobs.emplace_back(new job([=, &range]()
					{
						for (auto it = range.begin() + offset; it < range.end(); it += stride)
						{
							work(*it);
						}
					}));
			}
		}

		template<typename T, typename F>
			requires std::invocable<F, T&>
		static std::function<void()> create_data_job_func(F&& func, std::shared_ptr<job_data<T>> data)
		{
			if constexpr(std::is_rvalue_reference_v<F&&>)
			{
				//move it in
				return [data = std::move(data), f = std::move(func)](){f(extract(data)); };
			}
			else
			{
				//copy as normal
				return [=, data = std::move(data)]() {func(extract(data)); };
			}
		}

		template<typename F, typename P>
		requires std::invocable<F, provider_underlying_type<P>&>
		std::function<bool()> create_data_job_func(F&& func, P& provider)
		{
			return [f = std::forward<F>(func), &provider] ()
			{
				auto handle = provider.get();
				if (handle.has())
				{
					if constexpr (std::is_same_v<std::invoke_result_t<F, provider_underlying_type<P>&>, bool>)
					{
						return f(handle.get());
					}
					else
					{
						f(handle.get());
						return true;
					}
				}
				else
				{
					return false;
				}
			};
		}

		template<typename F, typename P, typename P2, typename ... Ps>
		requires std::invocable<F, provider_underlying_type<P>&, provider_underlying_type<P2>&, provider_underlying_type<Ps...>&>
		std::function<bool()> create_data_job_func(F&& func, P& provider, P2& provider2, Ps& ... providers)
		{
			std::function<bool(provider_underlying_type<P2>& param2, provider_underlying_type<Ps>& ...)> next_func =
				[f = std::forward<F>(func), &provider](provider_underlying_type<P2>& param2, provider_underlying_type<Ps>& ... params)
			{
				auto handle = provider.get();
				if (handle.has())
				{
					if constexpr (std::is_same_v<std::invoke_result_t<F, provider_underlying_type<P>&, provider_underlying_type<P2>&, provider_underlying_type<Ps...>&>, bool>)
					{
						return f(handle.get(), param2, params ...);
					}
					else
					{
						f(handle.get(), param2, params ...);
						return true;
					}
				}
				else
				{
					return false;
				}
			};
			return create_data_job_func(next_func, provider2, providers ...);
		}

		void enqueue_job(const std::shared_ptr<job>& new_job)
		{
			if (context.pool == this)
			{
				worker_threads[context.runner_index].work_queue.push(new_job);
			}
			else
			{
				unassigned_jobs.emplace(new_job);
			}
		}

		static void run_worker(thread_pool* pool, int worker_index)
		{
			pool->worker_threads[worker_index].run(pool);
		}
		
		rigtorp::mpmc::Queue<std::shared_ptr<job>> unassigned_jobs;
		std::deque<worker> worker_threads;
		std::atomic_flag exiting;

		inline static thread_local detail::thread_context context = { nullptr, -1 };
	};

	execution_context spool::thread_pool::get_execution_context()
	{
		if (thread_pool::context.pool != nullptr)
		{
			return { thread_pool::context.pool, thread_pool::context.pool->worker_threads[thread_pool::context.runner_index].active_job };
		}
		else return { nullptr, nullptr };
	}

	void thread_pool::worker::run(thread_pool* pool)
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
						pool->worker_threads[worker_index].work_queue.push(held_jobs.back());
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
					pool->worker_threads[worker_index].work_queue.push(held_jobs.back());
					held_jobs.pop_back();
				}
			}
		}
	}
	
}