#pragma once

#include "fgep/core/time.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace fgep::net {


struct UdpReceiverConfig {
    std::string bind_ipv4{"0.0.0.0"};
    std::uint16_t bind_port{0};
    bool enable_kernel_timestamping{false};
};

struct UdpReceiveResult {
    bool received{false};
    std::size_t bytes_received{0};
    TimestampNs receive_timestamp_ns{0};

     bool kernel_software_timestamp_available{false};
    TimestampNs kernel_software_timestamp_ns{0};
    bool kernel_hardware_timestamp_available{false};
    TimestampNs kernel_hardware_timestamp_ns{0};
};

class UdpReceiver {
public:
    UdpReceiver();

    explicit UdpReceiver(UdpReceiverConfig config);

    ~UdpReceiver();

    UdpReceiver(const UdpReceiver&) = delete;
    UdpReceiver& operator=(const UdpReceiver&) = delete;

    UdpReceiver(UdpReceiver&& other) noexcept;
    UdpReceiver& operator=(UdpReceiver&& other) noexcept;

    // Blocks until a datagram arrives or the underlying recv call reports
    // an error. A datagram larger than `buffer` is truncated by the kernel
    // per normal UDP semantics — result.bytes_received reports what fit,
    // not what arrived on the wire.
    [[nodiscard]] UdpReceiveResult receive(
        std::span<std::byte> buffer
    ) noexcept;

    [[nodiscard]] bool open() const noexcept;

    // True only if enable_kernel_timestamping was requested and the kernel
    // actually accepted the SO_TIMESTAMPING request at open time. False
    // does not mean receive() will fail — it means kernel timestamps will
    // never be populated, and receive() falls back to userspace timing.
    [[nodiscard]] bool kernel_timestamping_active() const noexcept;

    [[nodiscard]] std::uint16_t bound_port() const noexcept;

    [[nodiscard]] int native_socket() const noexcept;

    void close() noexcept;

private:
    UdpReceiverConfig config_{};
    int socket_fd_{-1};
    std::uint16_t bound_port_{0};
    bool manually_closed_{false};
    bool kernel_timestamping_active_{false};

    void open_socket() noexcept;
    void close_socket() noexcept;

    [[nodiscard]] UdpReceiveResult receive_plain(
        std::span<std::byte> buffer
    ) noexcept;
    [[nodiscard]] UdpReceiveResult receive_with_kernel_timestamps(
        std::span<std::byte> buffer
    ) noexcept;
};

} // namespace fgep::net