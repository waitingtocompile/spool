#pragma once
#include <atomic>
#include <functional>
#include <ranges>
#include <utility>
#include <memory>

#include "MPMCQueue.h"
#include "concepts.h"
#include "prerequisite.h"

namespace spool
{
    constexpr size_t max_job_prerequisites = 1024;
    class thread_pool;

    class job final : public detail::prerequisite_base
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

        void add_prerequisite(std::shared_ptr<detail::prerequisite_base> other)
        {
            if (other != nullptr && !(other->is_done()))
            {
                prerequisites.push(other);
            }
        }

        bool is_done()
        {
            return done.test();
        }

    private:

        job(const std::function<void()>& work)
            :work(work),
            prerequisites(max_job_prerequisites)
        {}

        template<prerequisite_range R>
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
            std::shared_ptr<detail::prerequisite_base> p;
            while (prerequisites.try_pop(p))
            {
                if (!p->is_done())
                {
                    //we've hit an un-matched semaphore, put it back and refuse to run
                    prerequisites.push(p);
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

        rigtorp::mpmc::Queue<std::shared_ptr<detail::prerequisite_base>> prerequisites;

        
    };
}
