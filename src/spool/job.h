#pragma once
#include <atomic>
#include <memory>
#include <functional>

namespace spool
{
    using shared_semaphore = std::shared_ptr<std::atomic_flag>;

    class thread_pool;

    class job
    {
        friend thread_pool;
    public:
        job(std::function<void()> work);

        template<typename I>
            requires std::input_iterator<I>&& std::convertible_to<std::iter_value_t<I>, job>
        job(std::function<void()> work, I prerequisites_first, I prerequisites_last);

        bool can_run();

        //prevents execution from starting if it hasn't already, but does not cancel or block dependant tasks
        void cancel();

        void add_prerequisite(job& other);

    private:
        std::function<void()> work;
        shared_semaphore completion_semaphore;
        //when a prerequisite is ready, it's removed from this list. New prerequisites can be added at runtime. Both together necessitates using the mpmc queue again
        rigtorp::mpmc::Queue<shared_semaphore> prerequisite_sempahores;
    };
}
