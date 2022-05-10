#pragma once
#include "concepts.h"
#include <atomic>

namespace spool
{
	template<typename T, typename R, typename Rp = R*>
	requires provides_read_handle<R, T> && dereferences_to<Rp, R>
	class read_provider final
	{
	public:
		read_provider(Rp resource)
			:resource(resource)
		{}

		auto get() const
		{
			return resource->create_read_handle();
		}

		R& get_resource() const
		{
			return *resource;
		}

	private:
		Rp resource;
	};

	template<typename T, typename R, typename Rp = R*>
	requires provides_write_handle<R, T> && dereferences_to<Rp, R>
	class write_provider final
	{
	public:
		write_provider(Rp resource)
			:resource(resource)
		{}

		auto get() const
		{
			return resource->create_write_handle();
		}

		R& get_resource() const
		{
			return *resource;
		}

	private:
		Rp resource;
	};

	template<typename T>
	class simple_handle final
	{
	public:
		explicit simple_handle(T* p_data)
			:data(p_data)
		{}

		bool has() const
		{
			return data;
		}

		T& get() const
		{
			return *data;
		}

	private:
		T* data;
	};

	template<typename R, typename T, auto fetch, auto del = []() {} >
	requires std::invocable<decltype(fetch), R&>&& std::invocable<decltype(del), R&> && invoke_result<decltype(fetch), T&, R&>
	class flexible_handle final
	{
	public:
		explicit flexible_handle(R* resource = nullptr)
			:resource(resource)
		{}

		flexible_handle(const flexible_handle&) = delete;

		bool has() const
		{
			return resource != nullptr;
		}

		T& get() const
		{
			return fetch(*resource);
		}

		~flexible_handle()
		{
			if (resource != nullptr)
			{
				del(*resource);
			}
		}

	private:
		R* resource;
	};

	//a simple shared resource wrapper, that allows for any number of readers and one writer
	template<typename T>
	class shared_resource final
	{
	public:
		template<typename ... Args>
		shared_resource(Args ... args)
			:data(args...)
		{}

		operator T& ()
		{
			return data;
		}

		T& get()
		{
			return data;
		}

		auto create_read_handle();

		auto create_write_handle();
		

		auto create_read_provider()
		{
			return read_provider<T, shared_resource>(this);
		}

		auto create_write_provider()
		{
			return write_provider<T, shared_resource>(this);
		}

	private:
		T data;
		std::atomic_int readers;
		std::atomic_flag writer;


		//these exist to satisfy the constexpr-ness constraints of template parameters

		static T& fetch(shared_resource& res)
		{
			return res.data;
		}

		static const T& fetch_const(shared_resource& res)
		{
			return res.data;
		}

		static void release_read(shared_resource& res)
		{
			res.readers--;
		}

		static void release_write(shared_resource& res)
		{
			res.writer.clear();
		}
	};

	template<typename T>
	auto shared_resource<T>::create_read_handle()
	{
		using read_handle = flexible_handle <shared_resource, const T, shared_resource::fetch_const, shared_resource::release_read> ;

		readers++;
		if (writer.test())
		{
			//there is a writer active, reset our read hold and return nothing
			readers--;
			return read_handle(nullptr);
		}
		else
		{
			return read_handle(this);
		}
	}

	template<typename T>
	auto shared_resource<T>::create_write_handle()
	{
		using write_handle = flexible_handle<shared_resource,	T, shared_resource::fetch, shared_resource::release_write>;

		if (writer.test_and_set())
		{
			//there's another writer active, do nothing
			return write_handle (nullptr);
		}
		else if (readers > 0)
		{
			//there's at least one active reader, release our write hold and do nothing
			writer.clear();
			return write_handle(nullptr);
		}
		else
		{
			return write_handle(this);
		}
	}
}