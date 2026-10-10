if(CONFIG_OWNTECH_TASK_API)
  # Select directory to add to the include path
  zephyr_include_directories(./public_api)

  # Define the current folder as a Zephyr library
  zephyr_library()

  # Select source files to be compiled
  zephyr_library_sources_ifdef(CONFIG_OWNTECH_TASK_IRQ_LOCK_TRACE src/irq_mask_trace.cpp)

  zephyr_library_sources(
    public_api/TaskAPI.cpp
    src/scheduling_common.cpp
    src/uninterruptible_synchronous_task.cpp
    src/asynchronous_tasks.cpp
    )
endif()
