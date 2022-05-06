#pragma once
#include <atomic>
#include <functional>
#include "concepts.h"
#include "shared_resource.h"
#include "memory"

namespace spool
{
	template<typename T>
	class input_data
	{
	public:
		template<typename ... Args>
			requires std::constructible_from<T, Args...>
		input_data(Args&& ... args)
			:data(std::forward<Args>(args) ...)
		{}

		simple_handle<const T> create_read_handle() const
		{
			return simple_handle<const T>(end_write.test() ? &data : nullptr);
		}

		void submit(const T& value)
		{
			if (!start_write.test_and_set())
			{
				data = value;
				end_write.test_and_set();
			}
		}

		void submit(T&& value)
		{
			if (!start_write.test_and_set())
			{
				data = std::move(value);
				end_write.test_and_set();
			}
		}

		template<typename F>
		requires std::invocable<F, T&>
		void submit(F& mutator)
		{
			if (!start_write.test_and_set())
			{
				mutator(data);
				end_write.test_and_set();
			}
		}

	private:
		T data;
		std::atomic_flag start_write;
		std::atomic_flag end_write;

	};
}