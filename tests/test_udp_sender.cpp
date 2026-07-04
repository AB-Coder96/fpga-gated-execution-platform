#include "fgep/net/udp_sender.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {

// Opens a UDP socket bound to an OS-assigned ephemeral port on loopback and
// reports which port it got, so this test never depends on any specific
// port being free. A short receive timeout keeps a failed test from hanging
// forever instead of failing fast.
[[nodiscard]] int open_loopback_listener(std::uint16_t& bound_port) {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    assert(fd >= 0);

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(0);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    const auto bind_result = ::bind(
        fd,
        reinterpret_cast<const sockaddr*>(&address),
        sizeof(address)
    );
    assert(bind_result == 0);

    sockaddr_in actual{};
    socklen_t actual_length = sizeof(actual);
    const auto name_result = ::getsockname(
        fd,
        reinterpret_cast<sockaddr*>(&actual),
        &actual_length
    );
    assert(name_result == 0);

    bound_port = ntohs(actual.sin_port);

    timeval timeout{};
    timeout.tv_sec = 1;
    timeout.tv_usec = 0;
    static_cast<void>(
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))
    );

    return fd;
}

} // namespace

int main() {
    using namespace fgep::net;

    // Default config has no destination port set: send() must not attempt a
    // real send, and open()/endpoint_valid() must report accordingly.
    {
        UdpSender sender{};
        assert(!sender.open());
        assert(!sender.endpoint_valid());

        const std::vector<std::byte> payload{
            std::byte{0x01}, std::byte{0x02}
        };
        const auto result = sender.send(payload);
        assert(!result.sent);
        assert(result.bytes_sent == 0);
        assert(result.send_timestamp_ns > 0);
    }

    // A real loopback send: bind a receiver on an OS-assigned port, point
    // the sender at it, and confirm the bytes arrive unchanged and the
    // reported send timestamp brackets the call.
    {
        std::uint16_t listener_port = 0;
        const int listener_fd = open_loopback_listener(listener_port);

        UdpSender sender{UdpSenderConfig{
            .destination_ipv4 = "127.0.0.1",
            .destination_port = listener_port
        }};
        assert(sender.open());
        assert(sender.endpoint_valid());

        const std::vector<std::byte> payload{
            std::byte{'h'}, std::byte{'i'}, std::byte{'!'}
        };

        const auto before = fgep::now_ns();
        const auto result = sender.send(payload);
        const auto after = fgep::now_ns();

        assert(result.sent);
        assert(result.bytes_sent == payload.size());
        assert(result.send_timestamp_ns >= before);
        assert(result.send_timestamp_ns <= after);

        std::array<std::byte, 16> received{};
        const auto received_bytes = ::recvfrom(
            listener_fd,
            received.data(),
            received.size(),
            0,
            nullptr,
            nullptr
        );
        assert(received_bytes == static_cast<ssize_t>(payload.size()));
        assert(
            std::memcmp(received.data(), payload.data(), payload.size()) == 0
        );

        static_cast<void>(::close(listener_fd));
    }

    // close() prevents further sends.
    {
        std::uint16_t listener_port = 0;
        const int listener_fd = open_loopback_listener(listener_port);

        UdpSender sender{UdpSenderConfig{
            .destination_ipv4 = "127.0.0.1",
            .destination_port = listener_port
        }};

        sender.close();
        assert(!sender.open());

        const std::vector<std::byte> payload{std::byte{0x01}};
        const auto result = sender.send(payload);
        assert(!result.sent);

        static_cast<void>(::close(listener_fd));
    }

    // Move semantics: the moved-from sender is closed and invalid, the
    // moved-to sender keeps sending correctly.
    {
        std::uint16_t listener_port = 0;
        const int listener_fd = open_loopback_listener(listener_port);

        UdpSender original{UdpSenderConfig{
            .destination_ipv4 = "127.0.0.1",
            .destination_port = listener_port
        }};

        UdpSender moved{std::move(original)};
        assert(moved.open());
        assert(!original.open());

        const std::vector<std::byte> payload{std::byte{0x09}};
        const auto result = moved.send(payload);
        assert(result.sent);

        std::array<std::byte, 4> received{};
        const auto received_bytes = ::recvfrom(
            listener_fd,
            received.data(),
            received.size(),
            0,
            nullptr,
            nullptr
        );
        assert(received_bytes == 1);

        static_cast<void>(::close(listener_fd));
    }

    // Empty payload is rejected without attempting a send, but still
    // captures a timestamp — the caller can still tell when the rejection
    // happened.
    {
        std::uint16_t listener_port = 0;
        const int listener_fd = open_loopback_listener(listener_port);

        UdpSender sender{UdpSenderConfig{
            .destination_ipv4 = "127.0.0.1",
            .destination_port = listener_port
        }};

        const auto result = sender.send({});
        assert(!result.sent);
        assert(result.send_timestamp_ns > 0);

        static_cast<void>(::close(listener_fd));
    }

    return 0;
}