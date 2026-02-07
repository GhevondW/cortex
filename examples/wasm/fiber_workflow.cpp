/**
 * @file fiber_workflow.cpp
 * @brief Demonstrates tiny_fiber cooperative multitasking in WebAssembly
 *
 * Uses Scheduler::Create() + Step() for clean WASM integration.
 * Each Step() runs one fiber until it yields, then returns to JS.
 */

#include <cmath>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include <cortex/config.hpp>
#include <cortex/tiny_fiber/tiny_fiber.hpp>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// clang-format off
EM_JS(void, js_log_message, (const char* msg), {
    if (typeof logMessage === 'function') {
        logMessage(UTF8ToString(msg));
    }
});

EM_JS(void, js_update_fiber, (int fiber_id, int state, int task_id), {
    if (typeof updateFiber === 'function') {
        updateFiber(fiber_id, state, task_id);
    }
});

EM_JS(void, js_add_task_to_queue, (int task_id, int complexity), {
    if (typeof addTaskToQueue === 'function') {
        addTaskToQueue(task_id, complexity);
    }
});

EM_JS(void, js_remove_task_from_queue, (int task_id), {
    if (typeof removeTaskFromQueue === 'function') {
        removeTaskFromQueue(task_id);
    }
});

EM_JS(void, js_task_completed, (int task_id, int worker_id), {
    if (typeof taskCompleted === 'function') {
        taskCompleted(task_id, worker_id);
    }
});

EM_JS(void, js_workflow_complete, (), {
    if (typeof workflowComplete === 'function') {
        workflowComplete();
    }
});

EM_JS(void, js_set_progress, (int completed, int total), {
    if (typeof setProgress === 'function') {
        setProgress(completed, total);
    }
});
// clang-format on
#endif

namespace tf = cortex::tiny_fiber;

// Fiber visual states
enum FiberVisualState {
    FIBER_IDLE = 0,
    FIBER_WORKING = 1,
    FIBER_WAITING = 2,
    FIBER_DONE = 3
};

namespace {

struct Task {
    int id;
    int complexity;
};

// Shared state
std::deque<Task> task_queue;
tf::Mutex* queue_mutex = nullptr;
tf::ConditionVariable* queue_cv = nullptr;
bool producer_done = false;
int tasks_completed = 0;
int total_tasks = 0;

// The scheduler instance for stepping
std::unique_ptr<tf::Scheduler> g_scheduler;

void log_msg(const std::string& msg) {
#ifdef __EMSCRIPTEN__
    js_log_message(msg.c_str());
#endif
    std::cout << msg << std::endl;
}

void update_fiber_state(int id, FiberVisualState state, int task_id = 0) {
#ifdef __EMSCRIPTEN__
    js_update_fiber(id, static_cast<int>(state), task_id);
#endif
}

// Producer fiber
void producer_fiber(int num_tasks) {
    log_msg("Producer: Starting");
    update_fiber_state(0, FIBER_WORKING, 0);
    tf::Yield();

    for (int i = 1; i <= num_tasks; ++i) {
        Task task {i, ((i - 1) % 5) + 1};

        update_fiber_state(0, FIBER_WORKING, i);
        log_msg("Producer: Creating task #" + std::to_string(i));
        tf::Yield();

        {
            auto guard = tf::Lock(*queue_mutex);
            task_queue.push_back(task);
#ifdef __EMSCRIPTEN__
            js_add_task_to_queue(task.id, task.complexity);
#endif
        }
        queue_cv->NotifyOne();

        log_msg("Producer: Queued task #" + std::to_string(i));
        tf::Yield();
    }

    {
        auto guard = tf::Lock(*queue_mutex);
        producer_done = true;
    }
    queue_cv->NotifyAll();

    update_fiber_state(0, FIBER_DONE, 0);
    log_msg("Producer: Done");
}

// Worker fiber
void worker_fiber(int worker_id) {
    std::string name = "Worker " + std::to_string(worker_id);
    log_msg(name + ": Ready");
    update_fiber_state(worker_id, FIBER_IDLE, 0);
    tf::Yield();

    while (true) {
        Task task {0, 0};
        bool got_task = false;

        {
            update_fiber_state(worker_id, FIBER_WAITING, 0);
            auto guard = tf::Lock(*queue_mutex);

            queue_cv->Wait(guard, [&] {
                return !task_queue.empty() || producer_done;
            });

            if (!task_queue.empty()) {
                task = task_queue.front();
                task_queue.pop_front();
                got_task = true;
#ifdef __EMSCRIPTEN__
                js_remove_task_from_queue(task.id);
#endif
            } else if (producer_done) {
                break;
            }
        }

        if (got_task) {
            update_fiber_state(worker_id, FIBER_WORKING, task.id);
            log_msg(name + ": Processing #" + std::to_string(task.id));

            // Simulate work - more yields for higher complexity
            // Each complexity unit = 3 yields for visible work time
            for (int i = 0; i < task.complexity * 3; ++i) {
                tf::Yield();
            }

            tasks_completed++;
#ifdef __EMSCRIPTEN__
            js_task_completed(task.id, worker_id);
            js_set_progress(tasks_completed, total_tasks);
#endif

            log_msg(name + ": Done #" + std::to_string(task.id));
            update_fiber_state(worker_id, FIBER_IDLE, 0);
            tf::Yield();
        }
    }

    update_fiber_state(worker_id, FIBER_DONE, 0);
    log_msg(name + ": Finished");
}

} // namespace

extern "C" {

CORTEX_API void start_workflow(int num_tasks, int num_workers) {
    // Reset state
    task_queue.clear();
    producer_done = false;
    tasks_completed = 0;
    total_tasks = num_tasks;

    log_msg("=== Starting Workflow ===");
    log_msg("Tasks: " + std::to_string(num_tasks) + ", Workers: " + std::to_string(num_workers));

#ifdef __EMSCRIPTEN__
    js_set_progress(0, total_tasks);
#endif

    // Create scheduler for manual stepping
    g_scheduler = tf::Scheduler::Create([num_tasks, num_workers] {
        // Create sync primitives on fiber stack
        tf::Mutex mutex;
        tf::ConditionVariable cv;
        queue_mutex = &mutex;
        queue_cv = &cv;

        // Spawn producer
        auto producer = tf::Spawn([num_tasks] {
            producer_fiber(num_tasks);
        });

        // Spawn workers
        std::vector<tf::Future<void>> workers;
        for (int i = 1; i <= num_workers; ++i) {
            workers.push_back(tf::Spawn([i] {
                worker_fiber(i);
            }));
        }

        // Wait for all
        producer.Wait();
        for (auto& w : workers) {
            w.Wait();
        }

        queue_mutex = nullptr;
        queue_cv = nullptr;

        log_msg("=== Workflow Complete! ===");
#ifdef __EMSCRIPTEN__
        js_workflow_complete();
#endif
    });
}

CORTEX_API int step_workflow() {
    if (g_scheduler && !g_scheduler->IsDone()) {
        return g_scheduler->Step() ? 1 : 0;
    }
    return 0;
}

CORTEX_API int is_workflow_done() {
    return (!g_scheduler || g_scheduler->IsDone()) ? 1 : 0;
}

} // extern "C"

int main() {
    std::cout << "Cortex Fiber Workflow Demo Ready" << std::endl;
    return 0;
}
