#include "fgep/net/udp_sender.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstddef>
#include <utility>

namespace fgep::net {
namespace {

[[nodiscard]] bool is_valid_ipv4(const std::string& address) noexcept {
    in_addr parsed{};

    return ::inet_pton(AF_INET, address.c_str(), &parsed) == 1;
}

[[nodiscard]] bool is_valid_endpoint(const UdpSenderConfig& config) noexcept {
    return config.destination_port != 0 && is_valid_ipv4(config.destination_ipv4);
}

[[nodiscard]] sockaddr_in make_destination(
    const UdpSenderConfig& config
) noexcept {
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(config.destination_port);

    static_cast<void>(
        ::inet_pton(
            AF_INET,
            config.destination_ipv4.c_str(),
            &destination.sin_addr
        )
    );

    return destination;
}

} // namespace

UdpSender::UdpSender() : UdpSender{UdpSenderConfig{}} {
}

UdpSender::UdpSender(UdpSenderConfig config)
    : config_{std::move(config)},
      endpoint_valid_{is_valid_endpoint(config_)} {
    open_socket();
}

UdpSender::~UdpSender() {
    close_socket();
}

UdpSender::UdpSender(UdpSender&& other) noexcept
    : config_{std::move(other.config_)},
      socket_fd_{other.socket_fd_},
      endpoint_valid_{other.endpoint_valid_},
      manually_closed_{other.manually_closed_} {
    other.socket_fd_ = -1;
    other.endpoint_valid_ = false;
    other.manually_closed_ = true;
}

UdpSender& UdpSender::operator=(UdpSender&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    close_socket();

    config_ = std::move(other.config_);
    socket_fd_ = other.socket_fd_;
    endpoint_valid_ = other.endpoint_valid_;
    manually_closed_ = other.manually_closed_;

    other.socket_fd_ = -1;
    other.endpoint_valid_ = false;
    other.manually_closed_ = true;

    return *this;
}

UdpSendResult UdpSender::send(std::span<const std::byte> payload) noexcept {
    UdpSendResult result{};
    result.send_timestamp_ns = now_ns();

    if (payload.empty()
        || !endpoint_valid_
        || manually_closed_
        || socket_fd_ < 0) {
        return result;
    }

    const auto destination = make_destination(config_);

    const auto sent = ::sendto(
        socket_fd_,
        payload.data(),
        payload.size(),
        0,
        reinterpret_cast<const sockaddr*>(&destination),
        static_cast<socklen_t>(sizeof(destination))
    );

    if (sent < 0) {
        return result;
    }

    const auto sent_size = static_cast<std::size_t>(sent);
    result.bytes_sent = sent_size;
    result.sent = sent_size == payload.size();

    return result;
}

bool UdpSender::open() const noexcept {
    return endpoint_valid_ && !manually_closed_ && socket_fd_ >= 0;
}

bool UdpSender::endpoint_valid() const noexcept {
    return endpoint_valid_;
}

int UdpSender::native_socket() const noexcept {
    return socket_fd_;
}

void UdpSender::close() noexcept {
    close_socket();
    manually_closed_ = true;
}

void UdpSender::open_socket() noexcept {
    if (!endpoint_valid_ || manually_closed_ || socket_fd_ >= 0) {
        return;
    }

    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
}

void UdpSender::close_socket() noexcept {
    if (socket_fd_ >= 0) {
        static_cast<void>(::close(socket_fd_));
        socket_fd_ = -1;
    }
}

} // namespace fgep::net