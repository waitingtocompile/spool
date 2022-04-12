#include <gtest/gtest.h>
#include <spool.h>
#include <array>
#include <unordered_map>

TEST(spool_test, StartsAndQuitsSafely)
{
	spool::thread_pool* p;
	EXPECT_NO_THROW(p = new spool::thread_pool);
	EXPECT_NO_THROW(delete p);
	
}

TEST(spool_test, DoesAnyWork)
{
	spool::thread_pool pool;
	std::atomic_flag didWork;
	pool.enqueue_job([&]() {didWork.test_and_set(); });
	std::this_thread::sleep_for(std::chrono::seconds(2));
	pool.wait_exit();
	ASSERT_TRUE(didWork.test()) << "Work was never performed";
}

TEST(spool_test, RespectsSequencing)
{
	//check that the parent job fully completes before work is done.

	spool::thread_pool pool;
	std::atomic_flag first_done;
	std::atomic_flag violated;
	std::atomic_flag done;

	auto dep = pool.enqueue_job([&]() 
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			first_done.test_and_set();
		});
	pool.enqueue_job([&]() 
		{
			if (!first_done.test())violated.test_and_set();
			done.test_and_set();
		}, dep);

	while (!done.test())
	{
		//wait until the second job is done
	}
	pool.exit();
	ASSERT_FALSE(violated.test()) << "Job ran before prerequisite";
}

TEST(spool_test, LoadBalances)
{
	//enqueue a bunch of jobs, held by a lock-spin thread to keep them all until the entire batch is enqueued

	spool::thread_pool pool(4);
	std::atomic_flag start;
	std::array<std::thread::id, 1000> ids;
	std::atomic_int counter = 0;
	
	//waits until "start" is tripped
	auto lock_spin = pool.enqueue_job([&]() {while (!start.test()) {}});

	for (size_t i = 0; i < ids.size(); i++)
	{
		pool.enqueue_job([&, i]() {ids[i] = std::this_thread::get_id(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); counter++; }, lock_spin);
	}
	start.test_and_set();
	
	while (counter.load() + 1 < ids.size())
	{
	}
	pool.exit();

	std::unordered_map<std::thread::id, int> map;
	
	for (auto& id : ids)
	{
		if (map.contains(id))
		{
			map[id]++;
		}
		else
		{
			map[id] = 1;
		}
	}

	//moderate misbalance should provoke a non-fatal failure
	//All or almost all the work being done on one thread should be a fatal failure
	//since we're locked at four threads for this, we'll define a "moderate" failure as one thread doing over 50% of work
	//an unnaceptable failure is one thread doing over 80% of the work

	for (auto& elem : map)
	{
		ASSERT_LT(elem.second, ids.size() * 0.8f) << "Over 80% of work is done on a single thread, load is not being properly balanced.";
		ASSERT_LT(elem.second, ids.size() / 2) << "Over 50% of work is being done on a single thread. Load may not be properly balanced.";
	}
}

TEST(spool_test, ExecutionContextGood)
{
	spool::thread_pool pool;
	std::atomic_flag done;
	std::atomic_flag violated_pool;
	std::atomic_flag violated_job;
	std::shared_ptr<spool::job> job;
	job = pool.enqueue_job([&]()
		{
			auto job_context = spool::thread_pool::get_execution_context();
			if (job_context.pool != &pool) violated_pool.test_and_set();
			if (job_context.active_job != job) violated_job.test_and_set();
			done.test_and_set();
		});

	auto context = spool::thread_pool::get_execution_context();
	EXPECT_EQ(context.pool, nullptr) << "Execution context pool offered on non-worker thread";
	EXPECT_EQ(context.active_job.get(), nullptr) << "Execution context job offered on non-worker thread";

	while (!done.test())
	{}
	EXPECT_FALSE(violated_pool.test()) << "Execution context offered non-matching pool on worker thread";
	EXPECT_FALSE(violated_job.test()) << "Execution context offered non-matching job on worker thread";
	
}

TEST(spool_test, EnqueueChildJob)
{
	spool::thread_pool pool;
	std::atomic_flag done;
	pool.enqueue_job([&]() {
		spool::thread_pool::get_execution_context().pool->enqueue_job([&]() {done.test_and_set(); });
		});
	auto end = std::chrono::system_clock::now() + std::chrono::seconds(2);
	while (!done.test() && end > std::chrono::system_clock::now())
	{
	}
	ASSERT_TRUE(done.test()) << "Second-order task did not run";
}

TEST(spool_test, ParallelFor)
{
	struct box
	{
		int i = 0;
	};

	spool::thread_pool pool;
	std::atomic_int waiting = 1000;
	//generate our list
	std::array<box, 1000> arr;
	pool.for_each(arr, [&](box& b)
		{
			b.i = 1;
			waiting--;
		});
	while (waiting.load() != 0)
	{
	}
	for (box& b : arr)
	{
		//all should be 1
		ASSERT_EQ(b.i, 1) << "One or more elements in parallel for-each was not altered or was altered incorrectly";
	}
}