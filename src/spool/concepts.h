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


	template<typename H, typename T>
	concept shared_resource_handle = requires (H h)
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


	/*
	* these are holdovers from when I tried to do single-object read/write distinguished providers. I'd like to revive the idea, but the templates are too complicated for me
	
	

	template<typename P, typename T>
	concept shared_resource_read_provider = requires(P p)
	{
		{p.get_read()} -> shared_resource_handle<const T>;
	};

	template<typename P, typename T>
	concept shared_resource_write_provider = requires(P p)
	{
		{p.get_write()} -> shared_resource_handle<T>;
	};

	template <typename P>
	using read_handle_type = std::invoke_result_t<decltype(&P::get_read), P>;

	template <typename P>
	using write_handle_type = std::invoke_result_t<decltype(&P::get_write), P>;

	template <typename H>
	using resource_handle_type = std::remove_reference_t<std::invoke_result<decltype(&H::get_write), H>>;

	template <typename P>
	using read_resource_type = std::remove_const_t<resource_handle_type<read_handle_type<P>>;

	template<typename P>
	using write_resource_type = resource_handle_type<read_handle_type<P>>;

	

	template<typename F, typename ... Ps>
	concept shared_write_suitable = !std::invocable<F, const read_resource_type<Ps...>*> &&
		std::invocable<F, write_resource_type<Ps...>&> &&
		(... && shared_resource_write_provider<Ps, write_resource_type<Ps>>);

	template<typename F, typename ... Ps>
	concept shared_read_suitable = !shared_write_suitable<F, Ps...> &&
		std::invocable<F, const read_resource_type<Ps...>&> &&
		(... && shared_resource_read_provider<Ps, read_resource_type<Ps>>);

	*/
}