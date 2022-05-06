#pragma once
#include <concepts>
#include <memory>
#include "prerequisite.h"

namespace spool
{
	template<typename T>
	class job_data;

	namespace detail
	{
		template<typename T>
		T& extract(const std::shared_ptr<job_data<T>>&);
	}

	template <typename T>
	class job_data final : public spool::detail::prerequisite_base
	{
	public:
		friend thread_pool;
		friend T& detail::extract<T>(const std::shared_ptr<job_data<T>>&);

		job_data()
			:data()
		{}

		template<typename ... Args>
		requires std::constructible_from<T, Args...>
		job_data(Args&& ... args)
			:data(std::forward<Args>(args) ...)
		{}
		
		void submit(const T& value)
		{
			if (!assign_started.test_and_set())
			{
				data = value;
				assign_finished.test_and_set();
			}
		}

		void submit(T&& value)
		{
			if (!assign_started.test_and_set())
			{
				data = std::move(value);
				assign_finished.test_and_set();
			}
		}

		template <typename F>
		requires std::invocable<F, T&>
		void submit(const F& mutator)
		{
			if (!assign_started.test_and_set())
			{
				mutator(data);
				assign_finished.test_and_set();
			}
		}

		bool is_done() override
		{
			return assign_finished.test();
		}

	private:
		T data;
		std::atomic_flag assign_started;
		std::atomic_flag assign_finished;
	};

	namespace detail
	{
		template<typename T>
		T& extract(const std::shared_ptr<job_data<T>>& data)
		{
			return data->data;
		}
	}
}