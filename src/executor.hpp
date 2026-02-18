#pragma once

// Global work-stealing thread pool via Taskflow.
//
// Provides a process-global tf::Executor singleton sized to
// std::thread::hardware_concurrency(). All internal parallelism
// (save, load, hash, sync) submits work through this executor.
//
// Internal header — not installed.

#include <taskflow/taskflow.hpp>

namespace automerge_cpp::detail {

// Process-global executor. Created on first use, destroyed at exit.
// Taskflow's executor uses a work-stealing scheduler internally.
inline auto global_executor() -> tf::Executor& {
    static auto executor = tf::Executor{};
    return executor;
}

}  // namespace automerge_cpp::detail
