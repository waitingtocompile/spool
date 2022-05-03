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
		job_data(Args&& ... args)
			:data(std::forward<Args>(args) ...)
		{

		}
		
		void submit(const T& value)
		{
			if (!assigned.test_and_set())
			{
				data = value;
			}
		}

		void submit(T&& value)
		{
			if (!assigned.test_and_set())
			{
				data = std::move(value);
			}
		}

		template <typename F>
		requires std::invocable<F, T&>
		void submit(const F& mutator)
		{
			if (!assigned.test_and_set())
			{
				mutator(data);
			}
		}

		bool is_done() override
		{
			return assigned.test();
		}

	private:
		T data;
		std::atomic_flag assigned;
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