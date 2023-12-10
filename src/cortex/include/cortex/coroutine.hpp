#ifndef SRC_CORTEX_INCLUDE_CORTEX_COROUTINE_HPP
#define SRC_CORTEX_INCLUDE_CORTEX_COROUTINE_HPP

#include <cortex/api/suspendable.hpp>
#include <cortex/error.hpp>
#include <cortex/execution.hpp>
#include <cortex/stack_allocator.hpp>

#include <functional>
#include <memory>

namespace cortex {

class coroutine : public api::flow {
public:
    struct resume_on_completed_coroutine : public error {
        using error::error;
    };

    struct suspend_on_not_started_coroutine : public error {
        using error::error;
    };

    struct routine_i {
        routine_i() = default;
        virtual ~routine_i() noexcept = default;

        virtual void run_routine() = 0;
    };

    class basic_routine : public routine_i {
        explicit basic_routine(std::function<void()>&& func);

    public:
        static std::unique_ptr<basic_routine> make(std::function<void()>&& func);

        ~basic_routine() noexcept override = default;

        void run_routine() override;

    private:
        std::function<void()> _func;
    };

private:
    coroutine(cortex::stack_allocator alloc, routine_i* routine);

public:
    static coroutine create(cortex::stack_allocator&& alloc, routine_i* routine);
    static coroutine create(routine_i* routine);
    static std::unique_ptr<basic_routine> make_routine(std::function<void()>&& func);

    ~coroutine() noexcept override = default;

    coroutine(const coroutine&) = delete;
    coroutine(coroutine&&) = delete;

    coroutine& operator=(const coroutine&) = delete;
    coroutine& operator=(coroutine&&) = delete;

    void resume();
    void suspend();
    [[nodiscard]] bool is_completed() const;

private:
    void run(api::suspendable& suspender) override;

public:
    bool _completed {false};
    routine_i* _routine {nullptr};
    api::suspendable* _suspender = nullptr;
    execution _exec;
};

} // namespace cortex

#endif
