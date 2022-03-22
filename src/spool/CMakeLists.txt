

add_library(spool spool.h
    thread_pool.h
    wsq.h)
set_target_properties(spool PROPERTIES LINKER_LANGUAGE CXX)