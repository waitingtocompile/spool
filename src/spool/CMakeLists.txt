

add_library(spool spool.h
    thread_pool.h
    job.h
    wsq.h
    MPMCQueue.h)
set_target_properties(spool PROPERTIES LINKER_LANGUAGE CXX)