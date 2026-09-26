/**
 * @file fiber_workflow.cpp
 * @brief Demonstrates tiny_fiber cooperative multitasking in WebAssembly
 *
 * A producer fiber feeds tasks into a bounded Channel; worker fibers take
 * them out and "work" on them (sleeping, as if waiting on I/O). The page
 * drives the scheduler with js/cortex.mjs's drive(), which runs the fibers
 * between browser tasks and sleeps while they all sleep.
 */

#include <chrono>
#include <memory>
#include <optional>
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
using namespace std::chrono_literals;

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

// Room for three tasks: when it is full, the producer waits for a worker.
constexpr std::size_t kQueueCapacity = 3;

int tasks_completed = 0;
int total_tasks = 0;

std::unique_ptr<tf::Scheduler> g_scheduler;

void log_msg(const std::string& msg) {
#ifdef __EMSCRIPTEN__
    js_log_message(msg.c_str());
#else
    std::cout << msg << std::endl;
#endif
}

void update_fiber_state(int id, FiberVisualState state, int task_id = 0) {
#ifdef __EMSCRIPTEN__
    js_update_fiber(id, static_cast<int>(state), task_id);
#endif
}

// Producer fiber: creates the tasks and closes the queue when done.
void producer_fiber(tf::Channel<Task>& queue, int num_tasks) {
    tf::SetFiberName("producer");
    log_msg("Producer: Starting");

    for (int i = 1; i <= num_tasks; ++i) {
        Task task {i, ((i - 1) % 5) + 1};

        update_fiber_state(0, FIBER_WORKING, i);
        log_msg("Producer: Creating task #" + std::to_string(i));
        tf::SleepFor(300ms);

        if (queue.Size() == queue.Capacity()) {
            update_fiber_state(0, FIBER_WAITING, i);
            log_msg("Producer: Queue full, waiting for a worker");
        }
        queue.Send(task); // waits while the queue is full
#ifdef __EMSCRIPTEN__
        js_add_task_to_queue(task.id, task.complexity);
#endif
        log_msg("Producer: Queued task #" + std::to_string(i));
    }

    queue.Close(); // workers finish once the queue is drained
    update_fiber_state(0, FIBER_DONE, 0);
    log_msg("Producer: Done");
}

// Worker fiber: takes tasks until the queue is closed and empty.
void worker_fiber(tf::Channel<Task>& queue, int worker_id) {
    const std::string name = "Worker " + std::to_string(worker_id);
    tf::SetFiberName(name);
    log_msg(name + ": Ready");

    while (true) {
        update_fiber_state(worker_id, FIBER_WAITING, 0);
        const std::optional<Task> task = queue.Receive(); // waits while empty
        if (!task) {
            break; // closed and drained
        }
#ifdef __EMSCRIPTEN__
        js_remove_task_from_queue(task->id);
#endif
        update_fiber_state(worker_id, FIBER_WORKING, task->id);
        log_msg(name + ": Processing #" + std::to_string(task->id));

        // Simulated work: a wait (like network or disk I/O) that parks only
        // this fiber; the others and the page keep going.
        tf::SleepFor(task->complexity * 400ms);

        tasks_completed++;
#ifdef __EMSCRIPTEN__
        js_task_completed(task->id, worker_id);
        js_set_progress(tasks_completed, total_tasks);
#endif
        log_msg(name + ": Done #" + std::to_string(task->id));
    }

    update_fiber_state(worker_id, FIBER_DONE, 0);
    log_msg(name + ": Finished");
}

} // namespace

extern "C" {

// Creates the workflow's scheduler; the page drives it with js/cortex.mjs.
CORTEX_API void* start_workflow(int num_tasks, int num_workers) {
    tasks_completed = 0;
    total_tasks = num_tasks;

    log_msg("=== Starting Workflow ===");
    log_msg("Tasks: " + std::to_string(num_tasks) + ", Workers: " + std::to_string(num_workers));

#ifdef __EMSCRIPTEN__
    js_set_progress(0, total_tasks);
#endif

    g_scheduler = tf::Scheduler::Create([num_tasks, num_workers] {
        tf::Channel<Task> queue(kQueueCapacity);

        auto producer = tf::Spawn([&queue, num_tasks] {
            producer_fiber(queue, num_tasks);
        });
        std::vector<tf::Future<void>> workers;
        for (int i = 1; i <= num_workers; ++i) {
            workers.push_back(tf::Spawn([&queue, i] {
                worker_fiber(queue, i);
            }));
        }

        producer.Get();
        for (auto& worker : workers) {
            worker.Get();
        }

        log_msg("=== Workflow Complete! ===");
#ifdef __EMSCRIPTEN__
        js_workflow_complete();
#endif
    });
    return g_scheduler.get();
}

} // extern "C"

int main() {
    log_msg("Cortex Fiber Workflow Demo Ready");
    return 0;
}
