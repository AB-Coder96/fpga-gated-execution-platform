#pragma once

#include "fgep/core/time.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace fgep::net {

// -----------------------------------------------------------------------------
// UDP send helper with per-packet send-timestamp capture
// -----------------------------------------------------------------------------
//

struct UdpSenderConfig {
    std::string destination_ipv4{"127.0.0.1"};
    std::uint16_t destination_port{0};
};

struct UdpSendResult {
    bool sent{false};
    std::size_t bytes_sent{0};
    TimestampNs send_timestamp_ns{0};
};

class UdpSender {
public:
    UdpSender();

    explicit UdpSender(UdpSenderConfig config);

    ~UdpSender();

    UdpSender(const UdpSender&) = delete;
    UdpSender& operator=(const UdpSender&) = delete;

    UdpSender(UdpSender&& other) noexcept;
    UdpSender& operator=(UdpSender&& other) noexcept;

    [[nodiscard]] UdpSendResult send(
        std::span<const std::byte> payload
    ) noexcept;

    [[nodiscard]] bool open() const noexcept;

    [[nodiscard]] bool endpoint_valid() const noexcept;

    [[nodiscard]] int native_socket() const noexcept;

    void close() noexcept;

private:
    UdpSenderConfig config_{};
    int socket_fd_{-1};
    bool endpoint_valid_{false};
    bool manually_closed_{false};

    void open_socket() noexcept;
    void close_socket() noexcept;
};

} // namespace fgep::net