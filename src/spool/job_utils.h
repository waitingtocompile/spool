#pragma once
#include <concepts>
#include <functional>
#include <ranges>
#include <algorithm>

#include "job.h"
#include "concepts.h"
#include "job_data.h"

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
	
	template<typename F, typename T>
	requires std::invocable<F, T&>
	std::function<void()> create_data_job_func(F&& func, std::shared_ptr<job_data<T>> data)
	{
		return[func = std::forward<F>(func), data = std::move(data)](){func(extract(data)); };
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

	/* old recursive approach, remove once new approach is tested

		template<typename F, typename P>
		requires std::invocable<F, provider_underlying_type<P>&>
		static std::function<bool()> create_shared_job_func(F&& func, const P& provider)
		{
			//base case
			return[f = std::forward<F>(func), provider]()
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
		static std::function<bool()> create_shared_job_func(F&& func, const P& provider, const P2& provider2, const Ps& ... providers)
		{

			std::function<bool(provider_underlying_type<P2>& param2, provider_underlying_type<Ps>& ...)> next_func =
				[f = std::forward<F>(func), provider](provider_underlying_type<P2>& param2, provider_underlying_type<Ps>& ... params)
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
			return create_shared_job_func(next_func, provider2, providers ...);
		}

		*/
}