#pragma once

#include <cortex/tiny_fiber/detail/wait_queue.hpp>
#include <cortex/tiny_fiber/scheduler.hpp>

#include <cstddef>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

/**
 * @file channel.hpp
 * @brief FIFO message passing between fibers.
 */

namespace cortex::tiny_fiber {

/**
 * @class Channel
 * @brief A FIFO queue that fibers send values into and receive values from,
 *        suspending (not blocking the thread) while it is full or empty.
 *
 * Bounded (capacity >= 1) or unbounded. Close() ends the stream: receivers
 * drain what is buffered, then get std::nullopt; senders get false. Iterate
 * with range-for to receive until closed.
 *
 * A channel belongs to one scheduler. TrySend(), TryReceive() and Close()
 * never suspend, so plain code — an event callback between scheduler steps,
 * a JavaScript handler — can use them to feed fibers. A channel created with
 * an explicit scheduler (from plain code) is assumed to be fed from outside:
 * fibers waiting on it make the scheduler report kWaiting, not kDeadlocked.
 *
 * Like Mutex, a channel must outlive the fibers that use it.
 */
template <typename T>
class Channel {
public:
    static constexpr std::size_t kUnbounded = std::numeric_limits<std::size_t>::max();

    /**
     * @brief Create a channel on the current scheduler (call from a fiber).
     * @throws std::invalid_argument if capacity is 0.
     */
    explicit Channel(std::size_t capacity = kUnbounded)
        : Channel(Scheduler::Current(), capacity, /*external=*/false) {}

    /**
     * @brief Create a channel on `scheduler`, to be fed from plain code.
     * @throws std::invalid_argument if capacity is 0.
     */
    explicit Channel(Scheduler& scheduler, std::size_t capacity = kUnbounded)
        : Channel(scheduler, capacity, /*external=*/true) {}

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) = delete;
    Channel& operator=(Channel&&) = delete;
    ~Channel() = default;

    /**
     * @brief Send a value, suspending the current fiber while the channel is
     *        full.
     * @return false if the channel is closed (the value is dropped).
     * @throws SchedulerStoppingError / CancelledError while waiting.
     */
    template <typename U>
    bool Send(U&& value) {
        Scheduler& scheduler = FiberScheduler();
        for (;;) {
            scheduler.ThrowIfInterrupted(true);
            if (closed_) {
                return false;
            }
            if (buffer_.size() < capacity_) {
                Push(std::forward<U>(value));
                return true;
            }
            senders_.Push(scheduler.PrepareWait());
            detail::ExternalWaitScope external(scheduler, external_);
            scheduler.ParkCurrent("Channel::Send", true);
        }
    }

    /**
     * @brief Send without suspending. Usable outside fibers.
     * @return false if the channel is full or closed.
     */
    template <typename U>
    bool TrySend(U&& value) {
        if (closed_ || buffer_.size() >= capacity_) {
            return false;
        }
        Push(std::forward<U>(value));
        return true;
    }

    /**
     * @brief Receive the next value, suspending the current fiber while the
     *        channel is empty.
     * @return std::nullopt once the channel is closed and drained.
     * @throws SchedulerStoppingError / CancelledError while waiting.
     */
    std::optional<T> Receive() {
        Scheduler& scheduler = FiberScheduler();
        for (;;) {
            scheduler.ThrowIfInterrupted(true);
            if (!buffer_.empty()) {
                return Pop();
            }
            if (closed_) {
                return std::nullopt;
            }
            receivers_.Push(scheduler.PrepareWait());
            detail::ExternalWaitScope external(scheduler, external_);
            scheduler.ParkCurrent("Channel::Receive", true);
        }
    }

    /**
     * @brief Receive without suspending. Usable outside fibers.
     * @return std::nullopt if the channel is empty.
     */
    std::optional<T> TryReceive() {
        if (buffer_.empty()) {
            return std::nullopt;
        }
        return Pop();
    }

    /**
     * @brief No more values will be sent. Wakes every waiting fiber.
     *        Usable outside fibers. Idempotent.
     */
    void Close() {
        closed_ = true;
        if (Scheduler* scheduler = LiveScheduler()) {
            receivers_.WakeAll(*scheduler);
            senders_.WakeAll(*scheduler);
        }
    }

    [[nodiscard]] bool IsClosed() const noexcept {
        return closed_;
    }

    /// Number of buffered values.
    [[nodiscard]] std::size_t Size() const noexcept {
        return buffer_.size();
    }

    [[nodiscard]] std::size_t Capacity() const noexcept {
        return capacity_;
    }

    /**
     * @class Iterator
     * @brief Input iterator that receives values until the channel closes.
     */
    class Iterator {
    public:
        using iterator_category = std::input_iterator_tag;
        using value_type = T;
        using difference_type = std::ptrdiff_t;
        using reference = T&;
        using pointer = T*;

        Iterator() = default;

        reference operator*() const {
            return *current_;
        }

        pointer operator->() const {
            return &*current_;
        }

        Iterator& operator++() {
            Advance();
            return *this;
        }

        void operator++(int) {
            Advance();
        }

        friend bool operator==(const Iterator& it, std::default_sentinel_t) noexcept {
            return it.channel_ == nullptr;
        }

    private:
        friend class Channel;

        explicit Iterator(Channel* channel)
            : channel_(channel) {
            Advance();
        }

        void Advance() {
            current_ = channel_->Receive();
            if (!current_) {
                channel_ = nullptr;
            }
        }

        Channel* channel_ {nullptr};
        // mutable: an input iterator hands out the received value from a
        // const operator*, and the value may be moved from.
        mutable std::optional<T> current_;
    };

    /// Receive the first value (suspends while empty).
    Iterator begin() {
        return Iterator(this);
    }

    [[nodiscard]] std::default_sentinel_t end() const noexcept {
        return std::default_sentinel;
    }

private:
    Channel(Scheduler& scheduler, std::size_t capacity, bool external)
        : scheduler_(&scheduler)
        , alive_(scheduler.AliveTokenInternal())
        , capacity_(capacity)
        , external_(external) {
        if (capacity == 0) {
            throw std::invalid_argument("Channel capacity must be at least 1");
        }
    }

    [[nodiscard]] Scheduler* LiveScheduler() const noexcept {
        return alive_.expired() ? nullptr : scheduler_;
    }

    // The scheduler, checked to be the one running the calling fiber.
    Scheduler& FiberScheduler() const {
        if (Scheduler::TryCurrent() != scheduler_ || scheduler_->GetCurrentFiber() == nullptr) {
            throw std::logic_error(
                "Channel::Send()/Receive() must be called from a fiber of the channel's scheduler; "
                "use TrySend()/TryReceive() elsewhere");
        }
        return *scheduler_;
    }

    template <typename U>
    void Push(U&& value) {
        buffer_.emplace_back(std::forward<U>(value));
        if (Scheduler* scheduler = LiveScheduler()) {
            receivers_.WakeOne(*scheduler);
        }
    }

    T Pop() {
        T value = std::move(buffer_.front());
        buffer_.pop_front();
        if (Scheduler* scheduler = LiveScheduler()) {
            senders_.WakeOne(*scheduler);
        }
        return value;
    }

    Scheduler* scheduler_;
    std::weak_ptr<void> alive_;
    std::size_t capacity_;
    bool external_;
    bool closed_ {false};
    std::deque<T> buffer_;
    detail::WaitQueue senders_;
    detail::WaitQueue receivers_;
};

} // namespace cortex::tiny_fiber
