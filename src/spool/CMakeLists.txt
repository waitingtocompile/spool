

add_library(spool spool.h
    thread_pool.h
    job.h
    concepts.h
    job_utils.h
    shared_resource.h
    input_data.h
    wsq.h
    MPMCQueue.h)
set_target_properties(spool PROPERTIES LINKER_LANGUAGE CXX)
