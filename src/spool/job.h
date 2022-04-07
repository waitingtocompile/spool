#pragma once
#include <atomic>
#include <functional>
#include <ranges>
#include <utility>
#include <memory>

#include "MPMCQueue.h"

namespace spool
{
    constexpr size_t max_job_prerequisites = 1024;
    class thread_pool;

    class job
    {
        friend thread_pool;
    public:

        job(const job& other) = delete;
        job(job&& other) = delete;

        //prevents execution from starting if it hasn't already, but does not cancel or block dependant tasks
        void cancel()
        {
            done.test_and_set();
        }

        void add_prerequisite(std::shared_ptr<job> other)
        {
            if (other != nullptr && !(other->done.test()))
            {
                prerequisite_jobs.push(other);
            }
        }

        bool is_done()
        {
            return done.test();
        }

    private:

        job(const std::function<void()>& work)
            :work(work),
            prerequisite_jobs(max_job_prerequisites)
        {}

        template<typename R>
            requires std::ranges::input_range<R>&& std::convertible_to<std::iter_value_t<std::ranges::iterator_t<R>>, job*>
        job(const std::function<void()>& work, R prerequisites_range)
            :job(work)
        {
            std::ranges::for_each(prerequisites_range, [&](job* j) {add_prerequisite(j); });
        }      

        //returns true if the job is finished and should not be re-added to the queue
        bool try_run()
        {
            if (done.test())
            {
                //skip working if it's already "done"
                return true;
            }
            std::shared_ptr<job> p;
            while (prerequisite_jobs.try_pop(p))
            {
                if (!p->done.test())
                {
                    //we've hit an un-matched semaphore, put it back and refuse to run
                    prerequisite_jobs.push(p);
                    return false;
                }
            }

            //we aren't watiting on any prerequisites, actually run
            work();

            done.test_and_set();
            return true;
        }

        std::function<void()> work;
        std::atomic_flag done;
        //when a prerequisite is ready, it's removed from this list. New prerequisites can be added at runtime. Both together necessitates using the mpmc queue again
        rigtorp::mpmc::Queue<std::shared_ptr<job>> prerequisite_jobs;

        
    };
}
