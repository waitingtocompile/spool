#pragma once
#include <atomic>
#include <memory>
#include <functional>
#include <ranges>
#include <utility>

#include "MPMCQueue.h"

namespace spool
{
    using shared_semaphore = std::shared_ptr<std::atomic_flag>;
    constexpr size_t max_job_prerequisites = 1024;
    class thread_pool;

    class job
    {
        friend thread_pool;
    public:
        

        job(const std::function<void()>& work)
            :work(work),
            completion_semaphore(std::make_shared<std::atomic_flag>()),
            prerequisite_sempahores(max_job_prerequisites)
        {}

        job(std::function<void()>&& work)
            :work(std::forward<std::function<void()>>(work)),
            completion_semaphore(std::make_shared<std::atomic_flag>()),
            prerequisite_sempahores(max_job_prerequisites)
        {}

        template<typename R>
            requires std::ranges::input_range<R> && std::convertible_to<std::iter_value_t<std::ranges::iterator_t<R>>, shared_semaphore>
        job(std::function<void()>&& work, R prerequisites_range)
            :job(std::forward(work))
        {
            for (shared_semaphore& prerequisite : prerequisites_range)
            {
                prerequisite_sempahores.push(prerequisite);
            }
        }

        template<typename R>
            requires std::ranges::input_range<R>&& std::convertible_to<std::iter_value_t<std::ranges::iterator_t<R>>, shared_semaphore>
        job(const std::function<void()>& work, R prerequisites_range)
            :job(work)
        {
            for (shared_semaphore& prerequisite : prerequisites_range)
            {
                prerequisite_sempahores.push(prerequisite);
            }
        }

        job(std::function<void()>&& work, const shared_semaphore& prerequisite)
            :job(std::forward<std::function<void()>>(work))
        {
            prerequisite_sempahores.push(prerequisite);
        }

        job(const std::function<void()>& work, const shared_semaphore& prerequisite)
            :job(work)
        {
            prerequisite_sempahores.push(prerequisite);
        }

        job(const job& other) = delete;
        job(job&& other) = delete;

        //prevents execution from starting if it hasn't already, but does not cancel or block dependant tasks
        void cancel()
        {
            completion_semaphore->test_and_set();
        }

        void add_prerequisite(shared_semaphore& sem)
        {
            prerequisite_sempahores.push(sem);
        }

        operator shared_semaphore&()
        {
            return completion_semaphore;
        }

    private:
        //create a dummy job. It can never be run, but we need it to handle the otuptut-parameter wierdness of the MPMPQueue
        job()
            :prerequisite_sempahores(0)
        {}

        //returns true if the job is finished and should not be re-added to the queue
        bool try_run()
        {
            if (completion_semaphore->test())
            {
                return true;
            }

            shared_semaphore p;
            while (prerequisite_sempahores.try_pop(p))
            {
                if (!p->test())
                {
                    //we've hit an un-matched semaphore, put it back and refuse to run
                    prerequisite_sempahores.push(p);
                    return false;
                }
            }

            //we aren't watiting on any prerequisites, actually run
            work();

            completion_semaphore->test_and_set();
        }

        std::function<void()> work;
        shared_semaphore completion_semaphore;
        //when a prerequisite is ready, it's removed from this list. New prerequisites can be added at runtime. Both together necessitates using the mpmc queue again
        rigtorp::mpmc::Queue<shared_semaphore> prerequisite_sempahores;
    };
}
