#pragma once
#include <concepts>
#include <ranges>
#include <memory>
#include <functional>

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

	template<typename F>
	concept job_func = std::convertible_to<F, std::function<void()>>;

	template <typename R>
	concept prerequisite_range = std::ranges::input_range<R> &&
		std::convertible_to<range_underlying<R>, std::shared_ptr<detail::prerequisite_base>>;

	template <typename T>
	concept usable_prerequisite = std::convertible_to<T, std::shared_ptr<detail::prerequisite_base>>
		|| prerequisite_range<T>;

	template<typename H, typename T>
	concept shared_resource_handle = requires (const H h)
	{
		{h.get()} -> std::convertible_to<T&>;
		{h.has()} -> std::convertible_to<bool>;
	};

	template<typename P, typename T>
	concept shared_resource_provider = requires (P p)
	{
		{p.get()} -> shared_resource_handle<T>;
	};

	template<typename P>
	using offered_handle_type = std::remove_reference_t<std::invoke_result_t<decltype(&P::get), P>>;

	template<typename H>
	using handle_underlying_type = std::remove_reference_t<std::invoke_result_t<decltype(&H::get), H>>;

	template<typename P>
	using provider_underlying_type = handle_underlying_type<offered_handle_type<P>>;

	template<typename From, typename To>
	concept dereferenceable_to = requires(From f)
	{
		{*f} -> std::convertible_to<To&>;
	};

	template<typename F, typename ... Ps>
	concept takes_shared_providers = std::invocable<F, provider_underlying_type<Ps...>> &&
		(... && shared_resource_provider<Ps, provider_underlying_type<Ps>>);

	template <typename T, typename R>
	concept can_provide_read = requires(R r)
	{
		{r.create_read_handle()} -> shared_resource_handle<const T>;
		
	};

	template<typename T, typename R>
	concept can_provide_write = requires(R r)
	{
		{r.create_write_handle()} -> shared_resource_handle<T>;
	};
}