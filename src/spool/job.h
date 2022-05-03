#pragma once
#include <atomic>
#include <functional>
#include <ranges>
#include <utility>
#include <memory>
#include <variant>

#include "MPMCQueue.h"
#include "concepts.h"
#include "prerequisite.h"

namespace spool
{
    namespace detail
    {
        struct nil {};
        constexpr size_t max_job_prerequisites = 1024;
    }
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
        template<typename F>
            requires std::convertible_to<F, std::function<void()>> || std::convertible_to<F, std::function<bool()>>
        job(F && work, detail::nil = {})
            :work(std::forward<F>(work)),
            prerequisites(detail::max_job_prerequisites)
        {}

        template<typename F, prerequisite_range R>
            requires std::convertible_to<F, std::function<void()>> || std::convertible_to<F, std::function<bool()>>
        job(F && work, const R & prerequisites_range)
            : job(std::forward<F>(work))
        {
            std::ranges::for_each(prerequisites_range, [&](std::shared_ptr<detail::prerequisite_base> j) {add_prerequisite(j); });
        }

        template<typename F>
            requires std::convertible_to<F, std::function<void()>> || std::convertible_to<F, std::function<bool()>>
        job(F&& work, std::shared_ptr<detail::prerequisite_base> prerequisite)
            : job(std::forward<F>(work))
        {
            add_prerequisite(prerequisite);
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
                    //we've hit an un-matched prerequisite, put it back and refuse to run
                    prerequisites.push(p);
                    return false;
                }
            }

            //we aren't watiting on any prerequisites, actually run
            if (std::holds_alternative<std::function<void()>>(work))
            {
                std::get<std::function<void()>>(work)();
            }
            else if (!(std::get<std::function<bool()>>(work)()))
            {
                    return false;
            }
            

            done.test_and_set();
            return true;
        }

        std::variant<std::function<void()>, std::function<bool()>> work;
        std::atomic_flag done;

        rigtorp::mpmc::Queue<std::shared_ptr<detail::prerequisite_base>> prerequisites;
    };
}
