#include "fgep/net/udp_receiver.hpp"

#include <arpa/inet.h>
#include <linux/net_tstamp.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>
#include <utility>

namespace fgep::net {
namespace {

[[nodiscard]] bool is_valid_bind_address(
    const std::string& address
) noexcept {
    in_addr parsed{};

    return ::inet_pton(AF_INET, address.c_str(), &parsed) == 1;
}

constexpr int kernel_timestamping_flags = SOF_TIMESTAMPING_RX_SOFTWARE
    | SOF_TIMESTAMPING_SOFTWARE
    | SOF_TIMESTAMPING_RX_HARDWARE
    | SOF_TIMESTAMPING_RAW_HARDWARE;

[[nodiscard]] TimestampNs timespec_to_ns(const timespec& value) noexcept {
    return static_cast<TimestampNs>(value.tv_sec) * 1'000'000'000ULL
        + static_cast<TimestampNs>(value.tv_nsec);
}

[[nodiscard]] bool timespec_is_set(const timespec& value) noexcept {
    return value.tv_sec != 0 || value.tv_nsec != 0;
}

} // namespace

UdpReceiver::UdpReceiver() : UdpReceiver{UdpReceiverConfig{}} {
}

UdpReceiver::UdpReceiver(UdpReceiverConfig config)
    : config_{std::move(config)} {
    open_socket();
}

UdpReceiver::~UdpReceiver() {
    close_socket();
}

UdpReceiver::UdpReceiver(UdpReceiver&& other) noexcept
    : config_{std::move(other.config_)},
      socket_fd_{other.socket_fd_},
      bound_port_{other.bound_port_},
      manually_closed_{other.manually_closed_},
      kernel_timestamping_active_{other.kernel_timestamping_active_} {
    other.socket_fd_ = -1;
    other.bound_port_ = 0;
    other.manually_closed_ = true;
    other.kernel_timestamping_active_ = false;
}

UdpReceiver& UdpReceiver::operator=(UdpReceiver&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    close_socket();

    config_ = std::move(other.config_);
    socket_fd_ = other.socket_fd_;
    bound_port_ = other.bound_port_;
    manually_closed_ = other.manually_closed_;
    kernel_timestamping_active_ = other.kernel_timestamping_active_;

    other.socket_fd_ = -1;
    other.bound_port_ = 0;
    other.manually_closed_ = true;
    other.kernel_timestamping_active_ = false;

    return *this;
}

UdpReceiveResult UdpReceiver::receive(std::span<std::byte> buffer) noexcept {
    if (buffer.empty() || manually_closed_ || socket_fd_ < 0) {
        return UdpReceiveResult{};
    }

    if (kernel_timestamping_active_) {
        return receive_with_kernel_timestamps(buffer);
    }

    return receive_plain(buffer);
}

UdpReceiveResult UdpReceiver::receive_plain(
    std::span<std::byte> buffer
) noexcept {
    UdpReceiveResult result{};

    const auto received = ::recvfrom(
        socket_fd_,
        buffer.data(),
        buffer.size(),
        0,
        nullptr,
        nullptr
    );

    result.receive_timestamp_ns = now_ns();

    if (received < 0) {
        return result;
    }

    result.received = true;
    result.bytes_received = static_cast<std::size_t>(received);

    return result;
}

UdpReceiveResult UdpReceiver::receive_with_kernel_timestamps(
    std::span<std::byte> buffer
) noexcept {
    UdpReceiveResult result{};

    iovec iov{};
    iov.iov_base = buffer.data();
    iov.iov_len = buffer.size();

    alignas(alignof(std::max_align_t)) unsigned char control[512];

    msghdr header{};
    header.msg_iov = &iov;
    header.msg_iovlen = 1;
    header.msg_control = control;
    header.msg_controllen = sizeof(control);

    const auto received = ::recvmsg(socket_fd_, &header, 0);

    result.receive_timestamp_ns = now_ns();

    if (received < 0) {
        return result;
    }

    result.received = true;
    result.bytes_received = static_cast<std::size_t>(received);

    for (cmsghdr* cmsg = CMSG_FIRSTHDR(&header);
         cmsg != nullptr;
         cmsg = CMSG_NXTHDR(&header, cmsg)) {
        if (cmsg->cmsg_level != SOL_SOCKET
            || cmsg->cmsg_type != SCM_TIMESTAMPING) {
            continue;
        }

        timespec timestamps[3]{};
        std::memcpy(timestamps, CMSG_DATA(cmsg), sizeof(timestamps));

        if (timespec_is_set(timestamps[2])) {
            result.kernel_hardware_timestamp_available = true;
            result.kernel_hardware_timestamp_ns =
                timespec_to_ns(timestamps[2]);
        }

        if (timespec_is_set(timestamps[0])) {
            result.kernel_software_timestamp_available = true;
            result.kernel_software_timestamp_ns =
                timespec_to_ns(timestamps[0]);
        }

        break;
    }

    return result;
}

bool UdpReceiver::open() const noexcept {
    return !manually_closed_ && socket_fd_ >= 0;
}

bool UdpReceiver::kernel_timestamping_active() const noexcept {
    return kernel_timestamping_active_;
}

std::uint16_t UdpReceiver::bound_port() const noexcept {
    return bound_port_;
}

int UdpReceiver::native_socket() const noexcept {
    return socket_fd_;
}

void UdpReceiver::close() noexcept {
    close_socket();
    manually_closed_ = true;
}

void UdpReceiver::open_socket() noexcept {
    if (manually_closed_ || socket_fd_ >= 0) {
        return;
    }

    if (!is_valid_bind_address(config_.bind_ipv4)) {
        return;
    }

    const auto fd = ::socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        return;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.bind_port);
    static_cast<void>(
        ::inet_pton(AF_INET, config_.bind_ipv4.c_str(), &address.sin_addr)
    );

    if (::bind(
            fd,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)
        ) != 0) {
        static_cast<void>(::close(fd));
        return;
    }

    sockaddr_in actual{};
    socklen_t actual_length = sizeof(actual);

    if (::getsockname(
            fd,
            reinterpret_cast<sockaddr*>(&actual),
            &actual_length
        ) == 0) {
        bound_port_ = ntohs(actual.sin_port);
    }

    if (config_.enable_kernel_timestamping) {
        int flags = kernel_timestamping_flags;
        kernel_timestamping_active_ = ::setsockopt(
            fd,
            SOL_SOCKET,
            SO_TIMESTAMPING,
            &flags,
            sizeof(flags)
        ) == 0;
    }

    socket_fd_ = fd;
}

void UdpReceiver::close_socket() noexcept {
    if (socket_fd_ >= 0) {
        static_cast<void>(::close(socket_fd_));
        socket_fd_ = -1;
    }
}

} // namespace fgep::net