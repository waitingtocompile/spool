#pragma once
#include <concepts>
#include <functional>
#include <ranges>
#include <algorithm>

#include "job.h"
#include "concepts.h"

namespace spool::detail
{

	//we're putting up with this awkward mess until we can get the extended range views in c++23
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
	
	template<typename F, typename ... Hs>
	requires std::invocable<F, handle_underlying_type<Hs...>&>
	bool run_with_handles(const F& func, const Hs& ... handles)
	{
		const std::array<bool, sizeof...(Hs)> has{ handles.has()... };
		if (std::ranges::find(has, false) == has.end())
		{
			//all handles offer the desired type
			func(handles.get()...);
			return true;
		}
		else
		{
			return false;
		}
	}

	template<typename F, typename ... Ps>
	requires std::invocable<F, provider_underlying_type<Ps...>&>
	bool run_with_providers(F& func, Ps& ... providers)
	{
		return run_with_handles(func, providers.get()...);
	}

	template<typename F, typename ... Ps>
	requires std::invocable<F, provider_underlying_type<Ps...>&>
	std::function<bool()> create_shared_resource_job_func(F&& func, Ps&&... providers)
	{
		return[func = std::forward<F>(func), ... providers = std::forward<Ps>(providers)]()
		{
			return run_with_providers(func, providers...);
		};
	}
}