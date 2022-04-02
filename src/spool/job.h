#pragma once
#include <atomic>
#include <memory>
#include <functional>
#include <ranges>
#include <utility>

#include "MPMCQueue.h"

namespace spool
{
    constexpr size_t max_job_prerequisites = 1024;
    class thread_pool;

    class job_handle;

    namespace detail
    {
        class job
        {
            friend thread_pool;
            friend job_handle;
        public:

            job(const job& other) = delete;
            job(job&& other) = delete;

            //prevents execution from starting if it hasn't already, but does not cancel or block dependant tasks
            void cancel()
            {
                done.test_and_set();
            }

            ~job()
            {
                job* p;
                while (prerequisite_jobs.try_pop(p))
                {
                    release_hold(p);
                }
            }

        private:

            job(const std::function<void()>& work)
                :work(work),
                keep_alive(0),
                prerequisite_jobs(max_job_prerequisites)
            {}

            template<typename R>
                requires std::ranges::input_range<R>&& std::convertible_to<std::iter_value_t<std::ranges::iterator_t<R>>, job*>
            job(const std::function<void()>& work, R prerequisites_range)
                :job(work)
            {
                std::ranges::for_each(prerequisites_range, [&](job* j) {add_prerequisite(j); });
            }

            void add_prerequisite(job* other)
            {
                if (other != nullptr && !(other->done.test()))
                {
                    other->keep_alive++;
                    prerequisite_jobs.push(other);
                }
            }

            //returns true if the job is finished and should not be re-added to the queue
            bool try_run()
            {
                if (done.test())
                {
                    return true;
                }

                job* p;
                while (prerequisite_jobs.try_pop(p))
                {
                    if (!p->done.test())
                    {
                        //we've hit an un-matched semaphore, put it back and refuse to run
                        prerequisite_jobs.push(p);
                        return false;
                    }
                    else
                    {
                        release_hold(p);
                    }
                }

                //we aren't watiting on any prerequisites, actually run
                work();

                done.test_and_set();
                return true;
            }

            static void release_hold(job* j)
            {
                //callers are expected to have already done null checks
                assert(j != nullptr);
                int waiting = --(j->keep_alive);
                if (waiting == 0 && j->done.test())
                {
                    //if it's exactly zero (and not less than or equal to zero), and the job is done, it's our job to delete
                    delete j;
                }
            }

            static void offer_delete(job* j)
            {
                //callers are expected to have already done null checks
                assert(j != nullptr);
                if (j->keep_alive.load() == 0 && j->done.test())
                {
                    delete j;
                }
            }

            std::function<void()> work;
            std::atomic_flag done;
            //the number of other things keeping this job "alive" in memory to check against, not counting the thread pool
            std::atomic_int keep_alive;
            //when a prerequisite is ready, it's removed from this list. New prerequisites can be added at runtime. Both together necessitates using the mpmc queue again
            rigtorp::mpmc::Queue<job*> prerequisite_jobs;
        };
    }

    class job_handle
    {
    public:
        job_handle()
            :j(nullptr)
        {}

        job_handle(detail::job* j)
            :j(j)
        {
            j->keep_alive++;
        }

        job_handle(const job_handle& other)
            :job_handle(other.j)
        {}

        job_handle(job_handle&& other)
            :j(other.j)
        {
            if (&other != this)
            {
                other.j = nullptr;
            }
        }

        ~job_handle()
        {
            if (j != nullptr)
            {
                //relinquish our hold of this job
                detail::job::release_hold(j);
            }
        }

        job_handle& operator=(const job_handle& other) = default;
        job_handle& operator=(job_handle&& other) = default;

        operator detail::job* ()
        {
            //we aren't simply handing-off, we're also doing the test
            if (j != nullptr && j->done.test())
            {
                //release our hold on the job, we don't need it any more
                detail::job::release_hold(j);

                j = nullptr;
            }

            return j;
        }

        void cancel()
        {
            if (j != nullptr)
            {
                j->cancel();
                detail::job::release_hold(j);
                j = nullptr;
            }
        }

    private:
        detail::job* j;
    };
}
