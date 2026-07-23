#pragma once

#include <cstddef>
#include <memory>

#include <cortex/coroutine.hpp>
#include <cortex/coroutine_body.hpp>
#include <cortex/memory_resource.hpp>

/**
 * @file coroutine_pool.hpp
 * @brief Pool of reusable stackful coroutines.
 */

namespace cortex {

namespace detail {
class CoroutineImpl;

// Shared pool state (free list + config + locking policy). Defined in
// coroutine_pool.cpp; handles keep it alive via shared_ptr so they may
// safely outlive the pool object.
template <bool ThreadSafe>
struct BasicCoroutinePoolState;
} // namespace detail

/**
 * @struct CoroutinePoolConfig
 * @brief Configuration options for BasicCoroutinePool.
 */
struct CoroutinePoolConfig {
    /// Stack size for every coroutine created by the pool. One pool serves
    /// one stack size; use separate pools for different sizes.
    std::size_t stack_size_bytes = Coroutine::kDefaultStackSizeBytes;
    /// Retention cap: coroutines released beyond this are destroyed.
    std::size_t max_parked = 64;
    /// Resource used for impl and stack allocations on pool misses.
    /// @note The thread-safe pool calls this resource from multiple threads;
    /// PooledMemoryResource is not thread-safe, so keep the (thread-safe)
    /// default there.
    MemoryResourceSharedPtr memory_resource = GetDefaultMemoryResource();
};

template <bool ThreadSafe>
class BasicCoroutinePool;

/**
 * @class BasicPooledCoroutine
 * @brief Move-only handle to a reusable coroutine owned by a pool.
 *
 * Destruction (or Release()) returns the coroutine to its pool. The handle
 * itself is not thread-safe; the pool is (in the ThreadSafe instantiation).
 * After Release() or being moved from, a handle is empty: only Release() (a
 * no-op) and destruction are valid; any other call is undefined behavior
 * (assert-guarded in debug builds).
 */
template <bool ThreadSafe>
class BasicPooledCoroutine final {
public:
    BasicPooledCoroutine(BasicPooledCoroutine&& other) noexcept;
    BasicPooledCoroutine& operator=(BasicPooledCoroutine&& other) noexcept;
    BasicPooledCoroutine(const BasicPooledCoroutine&) = delete;
    BasicPooledCoroutine& operator=(const BasicPooledCoroutine&) = delete;

    /// Releases the coroutine back to the pool (see Release()).
    ~BasicPooledCoroutine();

    /**
     * @brief Resumes the coroutine; same semantics as Coroutine::Resume.
     * @throws ResumeOnDoneCoroutineError if the body already finished.
     */
    void Resume();

    /// @brief True once the current body has finished.
    [[nodiscard]] bool IsDone() const noexcept;

    /// @brief The coroutine's stack size in bytes.
    [[nodiscard]] std::size_t GetStackSize() const noexcept;

    /**
     * @brief Re-arms the coroutine with a new body, skipping the free list.
     *
     * The fastest reuse path: a tight loop can Rebind + Resume a held handle
     * without touching the pool. Valid when the current body finished or
     * never started.
     *
     * @throws std::logic_error if a started body has not finished.
     * @throws std::invalid_argument if the body is null.
     */
    void Rebind(CoroutineBody body);

    /**
     * @brief Returns the coroutine to the pool; the handle becomes empty.
     *
     * A started-but-unfinished body is force-unwound first: destructors on
     * the coroutine stack run, and a body with no suspend points executes to
     * completion during the unwind. If the pool is gone or full, the
     * coroutine is destroyed instead. No-op on an empty handle.
     * The released body's captures are dropped here (see BasicCoroutinePool).
     */
    void Release();

private:
    friend class BasicCoroutinePool<ThreadSafe>;

    BasicPooledCoroutine(detail::CoroutineImpl* impl,
                         std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state) noexcept;

    detail::CoroutineImpl* impl_ {nullptr};
    std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state_;
};

/**
 * @class BasicCoroutinePool
 * @brief Recycles whole coroutines: live context and stack, not just memory.
 *
 * Acquire() pops a parked coroutine and rebinds it to the new body — no
 * stack allocation and no context setup — falling back to creating a fresh
 * reusable coroutine when the free list is empty. Released coroutines park
 * with their context intact, up to CoroutinePoolConfig::max_parked. A
 * released body's captures are dropped at Release() time (the pool re-arms
 * the coroutine with an inert body), so the free list never pins user state.
 *
 * @tparam ThreadSafe true guards the free list with std::mutex; false uses a
 * no-op mutex and must only be used from a single thread. The ThreadSafe
 * instantiation targets native multithreaded use; WASM builds are
 * single-threaded today.
 */
template <bool ThreadSafe>
class BasicCoroutinePool final {
public:
    BasicCoroutinePool();

    /**
     * @throws std::invalid_argument if stack_size_bytes is 0 or
     * memory_resource is null.
     */
    explicit BasicCoroutinePool(CoroutinePoolConfig config);

    BasicCoroutinePool(const BasicCoroutinePool&) = delete;
    BasicCoroutinePool(BasicCoroutinePool&&) = delete;
    BasicCoroutinePool& operator=(const BasicCoroutinePool&) = delete;
    BasicCoroutinePool& operator=(BasicCoroutinePool&&) = delete;

    /// Closes the pool and destroys (force-unwinds) all parked coroutines.
    /// Outstanding handles stay valid and destroy their coroutine on release.
    ~BasicCoroutinePool();

    /**
     * @brief Pops a parked coroutine rebound to body, or creates a fresh one.
     *
     * Never waits for a coroutine to be released and never runs the body;
     * call Resume() on the handle.
     *
     * @throws std::invalid_argument if the body is null.
     */
    [[nodiscard]] BasicPooledCoroutine<ThreadSafe> Acquire(CoroutineBody body);

    /**
     * @brief Pre-creates parked coroutines until at least count are parked.
     *
     * Reserved coroutines are created unstarted (no context switches); the
     * count is capped by CoroutinePoolConfig::max_parked.
     *
     * @note Under concurrent Acquires the pool may be drained while Reserve
     * refills; the parked count never overshoots count or max_parked.
     */
    void Reserve(std::size_t count);

    /// @brief Number of coroutines currently parked in the free list.
    [[nodiscard]] std::size_t GetParkedCount() const;

private:
    std::shared_ptr<detail::BasicCoroutinePoolState<ThreadSafe>> state_;
};

/// Thread-safe pool (std::mutex-guarded free list).
using CoroutinePool = BasicCoroutinePool<true>;
/// Handle type produced by CoroutinePool.
using PooledCoroutine = BasicPooledCoroutine<true>;
/// Single-threaded pool: zero locking, same contract as PooledMemoryResource.
using LocalCoroutinePool = BasicCoroutinePool<false>;
/// Handle type produced by LocalCoroutinePool.
using LocalPooledCoroutine = BasicPooledCoroutine<false>;

} // namespace cortex
