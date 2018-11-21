set (sources
    apply.c
    benchmark.c
    init.c
    object.c
    once.c
    queue.c
    semaphore.c
    source.c
    time.c
)

if (COMPILER_HAS_BLOCKS_SUPPORT)
    list (APPEND sources
        data.c
        io.c
        transform.c
    )
endif ()

add_library(libdispatch_static STATIC ${sources})
add_library(libdispatch_shared SHARED ${sources})

foreach (target libdispatch_shared libdispatch_static)
    target_include_directories(${target}
        PUBLIC
        "${dispatch_SOURCE_DIR}"
        "${dispatch_BINARY_DIR}"

        PRIVATE
        "${dispatch_SOURCE_DIR}/os"
        "${dispatch_SOURCE_DIR}/private"
        ${KQUEUE_INCLUDE_DIRS}
        ${PTHREAD_WORKQUEUE_INCLUDE_DIRS}
        ${BLOCKS_RUNTIME_INCLUDE_DIRS}
    )
    target_link_libraries(${target}
        PRIVATE
        ${PTHREAD_WORKQUEUE_LIBRARIES}
        ${KQUEUE_LIBRARIES}
        ${BLOCKS_RUNTIME_LIBRARIES}
        rt
        -pthread
    )
endforeach ()

set_property(TARGET libdispatch_static libdispatch_shared PROPERTY
    OUTPUT_NAME dispatch)

set_target_properties(libdispatch_shared PROPERTIES 
    SOVERSION ${DISPATCH_ABI_VERSION}
    VERSION ${DISPATCH_VERSION}
)

install(
    TARGETS libdispatch_shared libdispatch_static
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
)

