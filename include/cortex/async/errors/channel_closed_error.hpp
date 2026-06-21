#pragma once

#include <stdexcept>

/**
 * @file channel_closed_error.hpp
 * @brief Exception thrown when sending on a closed channel.
 */

namespace cortex::async {

/**
 * @class ChannelClosedError
 * @brief Exception thrown when a channel operation fails because the channel is closed.
 */
class ChannelClosedError : public std::runtime_error {
public:
    ChannelClosedError()
        : std::runtime_error("Channel is closed") {}
};

} // namespace cortex::async
