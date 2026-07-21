#include <cortex/config.hpp>
#include <cortex/coroutine_pool.hpp>
#include <cortex/detail/null_mutex.hpp>

#include <cassert>
#include <mutex>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(CORTEX_EMSCRIPTEN)
#include "detail/coroutine_emscripten_impl.hpp"
#else
#include "detail/coroutine_native_impl.hpp"
#endif

namespace cortex {

namespace detail {

// Non-template pool logic. Locking is the wrapper's job. Deliberately NOT in
// an anonymous namespace: it is a member of the externally-declared
// BasicCoroutinePoolState template (GCC -Wsubobject-linkage).
struct PoolCore {
    explicit PoolCore(CoroutinePoolConfig config_arg)
        : config(std::move(config_arg)) {
        parked.reserve(16);
    }

    CoroutineImpl* CreateImpl(CoroutineBody body) const {
        const auto& resource = config.memory_resource;
        void* memory = resource->Allocate(sizeof(CoroutineImpl), alignof(CoroutineImpl));
        try {
            return new (memory) CoroutineImpl(std::move(body), config.stack_size_bytes, resource,
                                              /*reusable=*/true);
        } catch (...) {
            resource->Deallocate(memory, sizeof(CoroutineImpl), alignof(CoroutineImpl));
            throw;
        }
    }

    void DestroyImpl(CoroutineImpl* impl) const noexcept {
        const auto& resource = config.memory_resource;
        impl->~CoroutineImpl();
        resource->Deallocate(impl, sizeof(CoroutineImpl), alignof(CoroutineImpl));
    }

    CoroutinePoolConfig config;
    bool closed {false};
    std::vector<CoroutineImpl*> parked;
};

template <bool ThreadSafe>
struct BasicCoroutinePoolState {
    using Mutex = std::conditional_t<ThreadSafe, std::mutex, NullMutex>;

    explicit BasicCoroutinePoolState(CoroutinePoolConfig config)
        : core(std::move(config)) {}

    ~BasicCoroutinePoolState() {
        // Backstop: normally the pool destructor drains the free list; this
        // covers a state that dies together with the last outstanding handle.
        for (CoroutineImpl* impl : core.parked) {
            core.DestroyImpl(impl);
        }
    }

    PoolCore core;
    mutable Mutex mutex;
};

} // namespace detail

// ---------------------------------------------------------------------------
// BasicCoroutinePool

template <bool ThreadSafe>
BasicCoroutinePool<ThreadSafe>::BasicCoroutinePool()
    : BasicCoroutinePool(CoroutinePoolConfig {}) {}

template <bool ThreadSafe>
BasicCoroutinePool<ThreadSafe>::BasicCoroutinePool(CoroutinePoolConfig config) {
    if (config.stack_size_bytes == 0) {
        throw std::invalid_argument("stack_size_bytes is 0.");
    }
    if (!config.memory_resource) {
        throw std::invalid_argument("memory_resource is null.");
    }
    state_ = std::make_shared<detail::BasicCoroutinePoolState<ThreadSafe>>(std::move(config));
}

template <bool ThreadSafe>
BasicCoroutinePool<ThreadSafe>::~BasicCoroutinePool() {
    std::vector<detail::CoroutineImpl*> to_destroy;
    {
        std::scoped_lock lock(state_->mutex);
        state_->core.closed = true;
        to_destroy.swap(state_->core.parked);
    }
    // Destroy (force-unwind) outside the lock: the unwind of a parked
    // trampoline is trivial, but keep the pattern uniform with Release.
    for (auto* impl : to_destroy) {
        state_->core.DestroyImpl(impl);
    }
}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe> BasicCoroutinePool<ThreadSafe>::Acquire(CoroutineBody body) {
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }

    detail::CoroutineImpl* impl = nullptr;
    {
        std::scoped_lock lock(state_->mutex);
        if (!state_->core.parked.empty()) {
            impl = state_->core.parked.back();
            state_->core.parked.pop_back();
        }
    }

    if (impl) {
        // Exclusively owned once popped; rebind outside the lock.
        impl->Rebind(std::move(body));
    } else {
        impl = state_->core.CreateImpl(std::move(body));
    }

    return BasicPooledCoroutine<ThreadSafe>(impl, state_);
}

template <bool ThreadSafe>
void BasicCoroutinePool<ThreadSafe>::Reserve(std::size_t count) {
    if (count > state_->core.config.max_parked) {
        count = state_->core.config.max_parked;
    }
    for (;;) {
        {
            std::scoped_lock lock(state_->mutex);
            if (state_->core.closed || state_->core.parked.size() >= count) {
                return;
            }
        }
        // Created unstarted: Acquire's Rebind swaps the placeholder body out
        // before the trampoline ever runs, so reserving costs no switches.
        auto* impl = state_->core.CreateImpl([](CoroutineSuspendContext&) {});
        bool parked = false;
        try {
            std::scoped_lock lock(state_->mutex);
            if (!state_->core.closed && state_->core.parked.size() < count) {
                state_->core.parked.push_back(impl);
                parked = true;
            }
        } catch (...) {
            state_->core.DestroyImpl(impl);
            throw;
        }
        if (!parked) {
            state_->core.DestroyImpl(impl);
            return;
        }
    }
}

template <bool ThreadSafe>
std::size_t BasicCoroutinePool<ThreadSafe>::GetParkedCount() const {
    std::scoped_lock lock(state_->mutex);
    return state_->core.parked.size();
}

// ---------------------------------------------------------------------------
// BasicPooledCoroutine

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>::BasicPooledCoroutine(
    detail::CoroutineImpl* impl, std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state) noexcept
    : impl_(impl)
    , state_(std::move(state)) {}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>::BasicPooledCoroutine(BasicPooledCoroutine&& other) noexcept
    : impl_(std::exchange(other.impl_, nullptr))
    , state_(std::move(other.state_)) {}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>& BasicPooledCoroutine<ThreadSafe>::operator=(
    BasicPooledCoroutine&& other) noexcept {
    if (this != &other) {
        Release();
        impl_ = std::exchange(other.impl_, nullptr);
        state_ = std::move(other.state_);
    }
    return *this;
}

template <bool ThreadSafe>
BasicPooledCoroutine<ThreadSafe>::~BasicPooledCoroutine() {
    Release();
}

template <bool ThreadSafe>
void BasicPooledCoroutine<ThreadSafe>::Resume() {
    assert(impl_);
    impl_->Resume();
}

template <bool ThreadSafe>
bool BasicPooledCoroutine<ThreadSafe>::IsDone() const noexcept {
    assert(impl_);
    return impl_->IsDone();
}

template <bool ThreadSafe>
std::size_t BasicPooledCoroutine<ThreadSafe>::GetStackSize() const noexcept {
    assert(impl_);
    return impl_->GetStackSize();
}

template <bool ThreadSafe>
void BasicPooledCoroutine<ThreadSafe>::Rebind(CoroutineBody body) {
    assert(impl_);
    if (!static_cast<bool>(body)) {
        throw std::invalid_argument("coroutine body is null.");
    }
    impl_->Rebind(std::move(body));
}

template <bool ThreadSafe>
void BasicPooledCoroutine<ThreadSafe>::Release() {
    if (!impl_) {
        return;
    }
    auto* impl = std::exchange(impl_, nullptr);

    // Unwind a started-but-unfinished body before parking. Runs outside the
    // pool lock: destructors on the coroutine stack may re-enter the pool.
    impl->AbortBody();

    // Re-arm with an inert body so the released body's captures are dropped
    // now instead of staying pinned in the free list until the next Acquire.
    impl->Rebind([](CoroutineSuspendContext&) {});

    bool park = false;
    try {
        std::scoped_lock lock(state_->mutex);
        if (!state_->core.closed && state_->core.parked.size() < state_->core.config.max_parked) {
            state_->core.parked.push_back(impl);
            park = true;
        }
    } catch (...) {
        // Allocation failure growing the free list: destroying the coroutine
        // beats terminating out of a noexcept destructor.
    }
    if (!park) {
        state_->core.DestroyImpl(impl);
    }
    state_.reset();
}

// ---------------------------------------------------------------------------
// Explicit instantiations: the only two variants that exist.

template class BasicCoroutinePool<true>;
template class BasicCoroutinePool<false>;
template class BasicPooledCoroutine<true>;
template class BasicPooledCoroutine<false>;

} // namespace cortex
