#pragma once

/**
 * @file channel.hpp
 * @brief CSP-style channels for inter-fiber communication.
 */

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

namespace cortex::async::channel {

template <typename T>
class Sender;

template <typename T>
class Receiver;

namespace detail {

template <typename T>
struct ChannelState {};

} // namespace detail

/**
 * @brief Create a bounded channel with the given capacity.
 * @return A (Sender, Receiver) pair.
 */
template <typename T>
std::pair<Sender<T>, Receiver<T>> MakeChannel([[maybe_unused]] std::size_t capacity) {
    throw std::runtime_error("Not implemented yet");
}

/**
 * @brief Create an unbounded channel.
 * @return A (Sender, Receiver) pair.
 */
template <typename T>
std::pair<Sender<T>, Receiver<T>> MakeUnboundedChannel() {
    throw std::runtime_error("Not implemented yet");
}

/**
 * @class Sender
 * @brief Sending end of a channel.
 *
 * Move-only. Closing or destroying the sender signals the receiver
 * that no more values will arrive.
 *
 * @tparam T The value type.
 */
template <typename T>
class Sender {
public:
    Sender(const Sender&) = delete;
    Sender& operator=(const Sender&) = delete;

    Sender(Sender&& other) noexcept
        : state_(std::move(other.state_)) {}

    Sender& operator=(Sender&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~Sender() = default;

    /**
     * @brief Send a value. Suspends if the buffer is full.
     * @return false if the channel is closed (receiver dropped).
     */
    template <typename U>
    bool Send([[maybe_unused]] U&& value) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Try to send without suspending.
     * @return false if the buffer is full or the channel is closed.
     */
    template <typename U>
    bool TrySend([[maybe_unused]] U&& value) {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Close the sender side.
     *
     * The receiver will see all buffered items, then get std::nullopt.
     */
    void Close() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Check if the channel is closed.
     */
    [[nodiscard]] bool IsClosed() const noexcept {
        return false; // Not implemented yet
    }

private:
    friend std::pair<Sender<T>, Receiver<T>> MakeChannel<T>(std::size_t);
    friend std::pair<Sender<T>, Receiver<T>> MakeUnboundedChannel<T>();

    explicit Sender(std::shared_ptr<detail::ChannelState<T>> state)
        : state_(std::move(state)) {}

    std::shared_ptr<detail::ChannelState<T>> state_;
};

/**
 * @class Receiver
 * @brief Receiving end of a channel.
 *
 * Move-only. Supports range-based for loops.
 *
 * @tparam T The value type.
 */
template <typename T>
class Receiver {
public:
    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    Receiver(Receiver&& other) noexcept
        : state_(std::move(other.state_)) {}

    Receiver& operator=(Receiver&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_);
        }
        return *this;
    }

    ~Receiver() = default;

    /**
     * @brief Receive a value. Suspends if the buffer is empty.
     * @return std::nullopt when the channel is closed and drained.
     */
    std::optional<T> Receive() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Try to receive without suspending.
     * @return std::nullopt if the buffer is empty or channel is closed.
     */
    std::optional<T> TryReceive() {
        throw std::runtime_error("Not implemented yet");
    }

    /**
     * @brief Check if the channel is closed and drained.
     */
    [[nodiscard]] bool IsClosed() const noexcept {
        return false; // Not implemented yet
    }

    /**
     * @class Iterator
     * @brief Input iterator for range-based for loops over the channel.
     */
    class Iterator {
    public:
        using value_type = T;
        using difference_type = std::ptrdiff_t;

        Iterator() = default;
        explicit Iterator(Receiver<T>* receiver)
            : receiver_(receiver) {
            ++(*this); // prime first value
        }

        T& operator*() {
            return *current_;
        }

        Iterator& operator++() {
            if (receiver_) {
                current_ = receiver_->Receive();
                if (!current_) {
                    receiver_ = nullptr;
                }
            }
            return *this;
        }

        bool operator==(const Iterator& other) const noexcept {
            return receiver_ == other.receiver_;
        }

        bool operator!=(const Iterator& other) const noexcept {
            return !(*this == other);
        }

    private:
        Receiver<T>* receiver_ {nullptr};
        std::optional<T> current_;
    };

    Iterator begin() {
        return Iterator(this);
    }
    Iterator end() {
        return Iterator();
    }

private:
    friend std::pair<Sender<T>, Receiver<T>> MakeChannel<T>(std::size_t);
    friend std::pair<Sender<T>, Receiver<T>> MakeUnboundedChannel<T>();

    explicit Receiver(std::shared_ptr<detail::ChannelState<T>> state)
        : state_(std::move(state)) {}

    std::shared_ptr<detail::ChannelState<T>> state_;
};

} // namespace cortex::async::channel
