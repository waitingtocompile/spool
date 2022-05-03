#pragma once
#include "concepts.h"
#include <atomic>

namespace spool
{
	template<typename T, typename R>
	requires can_provide_read<T, R>
	class read_provider final
	{
	public:
		read_provider(R* resource)
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
		R* const resource;
	};

	template<typename T, typename R>
		requires can_provide_write<T, R>
	class write_provider final
	{
	public:
		write_provider(R* resource)
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
		R* const resource;
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

		struct read_handle final
		{
		public:
			read_handle(shared_resource* source)
				:source(source)
			{}

			read_handle(const read_handle& other) = delete;

			bool has() const
			{
				return source != nullptr;
			}

			const T& get() const
			{
				return source->data;
			}

			~read_handle()
			{
				if (source != nullptr) source->readers--;
			}

		private:
			shared_resource* const source;
			
		};

		struct write_handle final
		{
			write_handle(shared_resource<T>* source)
				:source(source)
			{}

			write_handle(const write_handle& other) = delete;

			bool has() const
			{
				return source != nullptr;
			}

			T& get() const
			{
				return source->data;
			}

			~write_handle()
			{
				if (source != nullptr) source->writer.clear();
			}

		private:
			shared_resource* const source;
		};

		auto create_read_handle()
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

		auto create_write_handle()
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

		auto create_read_provider()
		{
			return read_provider<T, shared_resource<T>>(this);
		}

		auto create_write_provider()
		{
			return write_provider<T, shared_resource<T>>(this);
		}

	private:
		T data;
		std::atomic_int readers;
		std::atomic_flag writer;
	};
}