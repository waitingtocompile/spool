#pragma once
#include <concepts>
#include "prerequisite.h"

namespace spool
{
	template <typename T>
	class job_data final : public spool::detail::prerequisite_base
	{
	public:
		friend thread_pool;
		template<T>
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
			data = value;
			assigned.test_and_set();
		}

		void submit(T&& value)
		{
			data = std::move(value);
			assigned.test_and_set();
		}

		void submit(const std::function<void(T&)>& mutator)
		{
			mutator(data);
			assigned.test_and_set();
		}

		bool is_done() override
		{
			return assigned.test();
		}


	private:
		T data;
		std::atomic_flag assigned;
	};
}