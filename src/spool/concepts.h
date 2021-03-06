#pragma once
#include <concepts>
#include <ranges>
#include <memory>
#include <functional>

namespace spool
{
	class job;
	class thread_pool;

	template<typename P, typename T>
	concept dereferences_to = (std::is_pointer_v<P> && std::convertible_to<P, T*>) || requires (P p)
	{
		{*p} -> std::convertible_to<T&>;
		{p.operator->()} -> std::convertible_to<T*>;
	};

	template<std::ranges::range R>
	using range_underlying = std::iter_value_t<std::ranges::iterator_t<R>>;

	template<typename T, typename R, typename ... P>
	concept invoke_result = std::convertible_to<std::invoke_result_t<T, P...>, R>;

	template<typename F>
	concept job_func = std::convertible_to<F, std::function<void()>>;

	template <typename R>
	concept prerequisite_range = std::ranges::input_range<R> &&
		std::convertible_to<range_underlying<R>, std::shared_ptr<job>>;

	template <typename T>
	concept usable_prerequisite = std::convertible_to<T, std::shared_ptr<job>>
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

	template<typename F, typename ... Ps>
	concept takes_shared_providers = std::invocable<F, provider_underlying_type<Ps>&...> &&
		(... && shared_resource_provider<Ps, provider_underlying_type<Ps>>);

	template <typename R, typename T>
	concept provides_read_handle = requires(R r)
	{
		{r.create_read_handle()} -> shared_resource_handle<const T>;
		
	};

	template<typename R, typename T>
	concept provides_write_handle = requires(R r)
	{
		{r.create_write_handle()} -> shared_resource_handle<T>;
	};
}