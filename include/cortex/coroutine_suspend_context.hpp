#pragma once

namespace cortex {

class CoroutineSuspendContext {
public:
    static void* operator new(std::size_t) = delete;
    static void* operator new[](std::size_t) = delete;

public:
    CoroutineSuspendContext(const CoroutineSuspendContext&) = delete;
    CoroutineSuspendContext(CoroutineSuspendContext&&) = delete;
    CoroutineSuspendContext& operator=(const CoroutineSuspendContext&) = delete;
    CoroutineSuspendContext& operator=(CoroutineSuspendContext&&) = delete;

    virtual void Suspend() = 0;

protected:
    virtual ~CoroutineSuspendContext() = default;
    CoroutineSuspendContext() = default;
};

} // namespace cortex
