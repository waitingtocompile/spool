#include <gtest/gtest.h>
#include <spool.h>
#include <array>
#include <unordered_map>

TEST(spool_test, InitsSafely)
{
	EXPECT_NO_FATAL_FAILURE(new spool::thread_pool());
}

TEST(spool_test, DoesAnyWork)
{
	spool::thread_pool pool;
	std::atomic_flag didWork;
	pool.enqueue_job([&]() {didWork.test_and_set(); });
	pool.wait_exit();
	ASSERT_TRUE(didWork.test()) << "Work was never performed";
}

TEST(spool_test, RespectsSequencing)
{
	spool::shared_semaphore sem = std::make_shared<std::atomic_flag>();
	spool::thread_pool pool;
	std::atomic_flag violated;
	std::atomic_flag done;
	pool.enqueue_job([&]() {if (!sem->test())violated.test_and_set(); done.test_and_set(); }, sem);
	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	sem->test_and_set();

	while (!done.test())
	{
	}
	pool.exit();
	ASSERT_FALSE(violated.test()) << "Job ran before prerequisite";
}

TEST(spool_test, LoadBalances)
{
	
	spool::shared_semaphore start_sem = std::make_shared<std::atomic_flag>();
	spool::thread_pool pool(4);
	std::array<std::thread::id, 1000> ids;
	std::atomic_int counter = 0;
	
	for (size_t i = 0; i < ids.size(); i++)
	{
		pool.enqueue_job([&, i]() {ids[i] = std::this_thread::get_id(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); counter++; }, start_sem);
	}

	start_sem->test_and_set();
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