#pragma once
#include <concepts>
#include <ranges>
#include <memory>

namespace spool
{
	class job;
	class thread_pool;

	namespace detail
	{
		class prerequisite_base;
	}

	template<std::ranges::range R>
	using range_underlying = std::iter_value_t<std::ranges::iterator_t<R>>;

	template <typename R>
	concept job_range = std::ranges::input_range<R> &&
		std::convertible_to<range_underlying<R>, std::shared_ptr<job>>;

	template <typename R>
	concept prerequisite_range = std::ranges::input_range<R> &&
		std::convertible_to<range_underlying<R>, std::shared_ptr<detail::prerequisite_base>>;
}