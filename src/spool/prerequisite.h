#pragma once

namespace spool
{
	namespace detail
	{
		class prerequisite_base
		{
		public:
			virtual bool is_done() = 0;
		};
	}
}