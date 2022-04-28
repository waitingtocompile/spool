#pragma once
#include "concepts.h"
#include <atomic>

namespace spool
{
	template<typename T, typename R>
	requires can_provide_read<T, R>
	class read_provider
	{
	public:
		read_provider(R* resource)
			:resource(resource)
		{}

		auto get()
		{
			return resource->create_read_handle();
		}

		R& get_resource()
		{
			return *resource;
		}

	private:
		R* resource;
	};

	template<typename T, typename R>
		requires can_provide_read<T, R>
	class write_provider
	{
	public:
		write_provider(R* resource)
			:resource(resource)
		{}

		auto get()
		{
			return resource->create_write_handle();
		}

		R& get_resource()
		{
			return *resource;
		}

	private:
		R* resource;
	};

	//a simple shared resource wrapper, that allows for any number of readers and one writer
	template<typename T>
	class shared_resource
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

		struct read_handle
		{
		public:
			read_handle(shared_resource<T>* source)
				:source(source)
			{

			}

			bool has()
			{
				return source != nullptr;
			}

			const T& get()
			{
				return source->data;
			}

			~read_handle()
			{
				if(source != nullptr) source->readers--;
			}

		private:
			shared_resource<T>* source;
			
		};

		struct write_handle
		{
			write_handle(shared_resource<T>* source)
				:source(source)
			{

			}

			bool has()
			{
				return source != nullptr;
			}

			T& get()
			{
				return source->data;
			}

			~write_handle()
			{
				if (source != nullptr) source->writer.clear();
			}

		private:
			shared_resource<T>* source;
		};

		read_handle get_read_handle()
		{
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

		write_handle get_write_handle()
		{
			if (writer.test_and_set())
			{
				//there's another writer active, do nothing
				return write_handle(nullptr);
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

		read_provider<T, shared_resource<T>> get_read_provider()
		{
			return read_provider<T, shared_resource<T>>(this);
		}

		write_provider<T, shared_resource<T>> get_write_provider()
		{
			return write_provider<T, shared_resource<T>>(this);
		}

	private:
		T data;
		std::atomic_int readers;
		std::atomic_flag writer;
	};
}