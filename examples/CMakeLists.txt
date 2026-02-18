add_executable(basic_usage basic_usage.cpp)
target_link_libraries(basic_usage PRIVATE automerge-cpp::automerge-cpp)

add_executable(collaborative_todo collaborative_todo.cpp)
target_link_libraries(collaborative_todo PRIVATE automerge-cpp::automerge-cpp)

add_executable(text_editor text_editor.cpp)
target_link_libraries(text_editor PRIVATE automerge-cpp::automerge-cpp)

add_executable(sync_demo sync_demo.cpp)
target_link_libraries(sync_demo PRIVATE automerge-cpp::automerge-cpp)

add_executable(thread_safe_demo thread_safe_demo.cpp)
target_link_libraries(thread_safe_demo PRIVATE automerge-cpp::automerge-cpp)

add_executable(parallel_perf_demo parallel_perf_demo.cpp)
target_link_libraries(parallel_perf_demo PRIVATE automerge-cpp::automerge-cpp)

add_executable(json_interop_demo json_interop_demo.cpp)
target_link_libraries(json_interop_demo PRIVATE automerge-cpp::automerge-cpp nlohmann_json::nlohmann_json)
