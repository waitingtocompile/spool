# Spool
Spool is a thread-pooling task system for C++20.

## Quick Start
Creating a thread pool and queueing up jobs is easy, all you need is to call enqueue\_job on a thread pool, passing a function. For instance you could give it a lambda like so: 
```c++
spool::thread_pool pool;
pool.enqueue_job([](){\* ... *\});
```
Every job queued can also be assigned prerequisites, primarily other jobs.
```c++
//add a single dependancy
auto first = pool.enqueue_job(myFunction);
auto second = pool.enqueue_job(myOtherFunction, second);

//add many dependancies using C++20s Ranges
std::array<std::shared_ptr<spool::job>, 2> jobs{first, second};
pool.enqueue_job(yetAnotherFunction, jobs);
```

Jobs are any `std::function<void()>`, though there are also some other options (though they are a little more involved)

## The Thread Pool and Execution Context
By default the thread pool will create `std:hardware_concurrency() - 1` worker threads. You can chose to instead pass in the number of worker threads to be created to the constructor.

The thread pool class offers a static function `get_execution_context()`, if this is called from a worker thread it can provide information on the thread pool that thread is a part of, the currently running job, and some additional information, which can be useful to do things like queue up a new job to be run, but only after the current job finishes. If called from a non-worker thread, it offers almost no information.

While Spool doesn't prevent you from maintaining multiple thread pools, doing so is strongly discouraged.

## Passing Data to Another Job
Spool offers a mechanism for passing data to another job, fittingly called `job_data` (for the data itself) and `data_job` (for the job receiving the data). To queue up a data job, simply call `spool::thread_pool::enqueue_job`, passing in a function that takes one (and only one) parameter, and optionaly a shared\_ptr to a `job_data` object of the corresponding type. If you don't pass one, a new one will be automatically created for you, and returned as part of the `data_job` object.

A data job will not run until data has been submitted (and any other prerequisites have been met). Data can be submitted either by passing it into the job data object directly, or by passing job data a mutator to create the desired object in-place.

For example:
```c++
void myFunction(int i);
void myOtherFunction(int i);

//...
auto first = pool.enqueue_job<int>(myFunction);
//we are re-using the same job data handle between the two jobs, they will both wait for and draw from the same underlying value
auto second = pool.enqueue_job<int>(myOtherFunction, first.data_handle);

//...
//by submitting, both first and second are now ready to start, and will run on worker threads at some point in the future
first.data_handle.submit(10);
```

Note that the job\_data handle doesn't do anything to mediate the safety of the stored data between threads, only passing the data to workers once it's ready. The user is responsible for ensuring that the data stored within is suitably thread safe.

## Managing Access To Shared Resources
Spool can manage resources that are shared across many jobs, with each job accessing an arbitrary number of shared resources. This is done using **handles** and **providers**, both of which are defined via concept.

**Providers** provide handles. All providers must offer a member function `get` which takes no parameters are returns a newly constructed handle of the appropriate type. Providers need to be copyable, and ideally should be trivially copyable, as they are stored by value by waiting jobs. All the handles that spool's build in templates offer are designed to be disposable.

**Handles** provide access to a resource. A handle must offer a member function `has`, which returns true if the handle can offer the resource, or false if it cannot. If a specific handle has *ever* returned true, it must be able to provide that resource until that handle is destroyed. It also must provide a member function `get`, which returns a reference to the resource, spool will never call get if `has` has not first returned true, and thus no error handling for trying to `get` an unavailible resource is needed. Handles are designed to be disposable, and your code should never interact with them except to return them in your custom providers.

While you can write your own providers and handles, spool also offers a helpful shared resource wrapper called `spool::shared_resource`, which offers both read and write providers, which in turn provide the appropraite handles, as well as the `read_provider` and `write_provider` templates, which can be used with any custom resource wrappers you might make.

`spool::shared_resource`, through it's providers, permits any number of readers at a time, so long as there are no writers, or one writer, so long as there are no other readers or writers. These providers can be extracted with `create_read_provider` and `create_write_provider` respectively.

To actually use shared resource providers in your code, pass in the function that needs those resources, followed by the resource providers to be used, like so:
```c++
void myFunction(const FirstResource& readonlyResource, SecondResource& readWriteResource);
spool::shared_resource<FirstResource> wrappedFirst;
spool::shared_resource<SecondResource> wrappedSecond;

//...
pool.enqueue_job(myFunction, wrappedFirst.create_read_provider(), wrappedSecond.create_write_provider());
```

### Writing a custom provider, handle or wrapper
TODO: look I do want to explain the whole process, but I want to give everything a bit of time to settle before I decree what The Right Way To Do It is.


