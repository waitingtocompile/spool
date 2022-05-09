# Spool
Spool is a header only thread-pooling task system for C++20. If you're looking for the string-pooling utility by the same name look here https://github.com/jeremyong/spool

Spool is a place to keep your threads and to keep them organised, without you needing to worry with the specifics.

## Quick Start
To start out with spool, just add src/spool to your include paths, and include `spool.h`, then all you need is a thread pool, and some jobs to queue up in it. A job is any function that doesn't need any parameters.

```C++
#include <spool.h>

void myFunction()
{
    //...
}

int main()
{
    spool::thread_pool pool;
    pool.enqueue_job(myFunction);
}
```

You can also enqueue lambdas, or other anonymous functions

```
pool.enqueue_job([](){\* ... *\});
```

Finally, jobs can be assigned as prerequisites of other jobs
```c++
auto first = pool.enqueue_job(myFunction);
auto second = pool.enqueue_job(myDependantFunction, first);

//assign many prerequisites by passing a ranges
pool.enqueue_job(myOtherDependantFunction, myRangeOfManyJobs);
```

### On MSVC
At time of writing, the main C++20 target in MSVC does not fully support the new ranges API, which spool makes use of. For the time being, use `/std:c++latest` to get everything to behave properly.

## The Thread Pool and Execution Context
By default the thread pool will create `std:hardware_concurrency() - 1` worker threads. You can chose to instead pass in the number of worker threads to be created to the constructor.

The thread pool class offers a static function `get_execution_context()`, if this is called from a worker thread it can provide information on the thread pool that thread is a part of, the currently running job, and some additional information, which can be useful to do things like queue up a new job to be run, but only after the current job finishes. If called from a non-worker thread, it offers almost no information.

## Managing Access to Shared Resources
An ongoing problem in building concurrent code is managing access to a shared resource between threads. Fortunately, spool offers you the tools to do this. A templated wrapper called `spool::shared_resource` can manage access to a single shared resource, permitting one writer or any number of readers to access at a time. For instance:

```c++
spool::shared_resource<int> mySharedInt;
pool.enqueue_shared_resource_job([](int& i){i = someFunction();}, mySharedInt.create_write_provider());
pool.enqueue_shared_resource_job([](const int& i){someOtherFunction(i);}, mySharedInt.create_read_provider());
```

### Creating Custom Shared Resource Wrappers
If `spool::shared_resource` doesn't offer the functionality you want, you can create your own custom wrappers. Managing your shared resources is done through **providers** and **handles**, both of which are defined in terms of C++20 concepts.

**Providers** create handles to a given shared resource. They need to offer a single public method `get` that takes no parameters and returns a handle. Providers generally need to be copyable, and ideally should be trivially copyable or at least cheaply copyable. Spool offers two existing provider templates, `read_provider` and `write_provider`, which expect to be able to call `create_read_handle` and `create_write_handle` on the wrapper that creates them respectively.

**Handles** are the actual source of the shared resource, and mediate access to it. They need to provider two methods: `has` and `get`. The `has` method must return a boolean, if it returns `true`, then it means that the handle is granting access and `get` needs to provide a valid reference to the resource, if it returns `false` then the handle is not granting access, another handle will be requested later and `get` will never be called. Spool offers two existing templates, `simple_handle` and `flexible_handle`. `simple_handle` just expects a pointer to the underlying resource, or `nullptr` if access should be denied. `flexible_handle` instead expects a pointer to the resource wrapper, or `nullptr` if access should be denied, but it can also call an arbitrary function with the wrapper to get the resource, and can perform some operation when the handle is destroyed, to represent releasing access to the resource and potentially making it availible to other threads.

## Passing Data to A Future Job
There are also mechanisms for handing off data to a future job, even when that data isn't known when the job is enqueued. by calling the `enqueue_data_job` method of a thread pool, and passing it a function that expects a single parameter. The returned `data_job` contains the job itself, and a handle to the input data. This handle can be kept and handed off to any other job to be eventually filled in by calling it's `submit` method, at which point the job receiving that data can run (assuming all it's other prerequisites are met), using that data to fill in it's parameter.

```c++
void myFunction(int i);
//...
auto myDataJob = pool.enqueue_data_job(myFunction);

//some time later
myDataJob.data->submit(10);

//a job calling `myFunction` will now run when the pool gets around to it, passing "10" as the parameter
```

## Parallel-For
Spool also lets you split a for-each operation across many threads in the pool by calling the `for_each` method on the pool, passing the range to iterate and a function to call with each element in the array. It returns a vector of the generated jobs.

```c++
std::vector<int> is{10, 24, 53, 18, 33};
pool.for_each(is, [](int& i){/* do something with i */});
```
