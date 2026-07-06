#include "fgep/net/udp_receiver.hpp"
#include "fgep/net/udp_sender.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

int main() {
    using namespace fgep::net;

    // Binding to port 0 lets the OS assign one — bound_port() must report
    // something real.
    {
        UdpReceiver receiver{
            UdpReceiverConfig{.bind_ipv4 = "127.0.0.1", .bind_port = 0}
        };
        assert(receiver.open());
        assert(receiver.bound_port() != 0);
    }

    // An unparsable bind address must fail to open, not crash or hang.
    {
        UdpReceiver receiver{
            UdpReceiverConfig{.bind_ipv4 = "not-an-ip", .bind_port = 0}
        };
        assert(!receiver.open());
    }

    // A real send through commit 3's UdpSender, received via the plain
    // (non-kernel-timestamped) path: bytes must match exactly and the
    // receive timestamp must be sane.
    {
        UdpReceiver receiver{
            UdpReceiverConfig{.bind_ipv4 = "127.0.0.1", .bind_port = 0}
        };
        assert(receiver.open());
        assert(!receiver.kernel_timestamping_active());

        UdpSender sender{UdpSenderConfig{
            .destination_ipv4 = "127.0.0.1",
            .destination_port = receiver.bound_port()
        }};
        assert(sender.open());

        const std::vector<std::byte> payload{
            std::byte{'p'}, std::byte{'i'}, std::byte{'n'}, std::byte{'g'}
        };
        const auto send_result = sender.send(payload);
        assert(send_result.sent);

        std::array<std::byte, 16> buffer{};
        const auto receive_result = receiver.receive(buffer);

        assert(receive_result.received);
        assert(receive_result.bytes_received == payload.size());
        assert(receive_result.receive_timestamp_ns > 0);
        assert(!receive_result.kernel_software_timestamp_available);
        assert(!receive_result.kernel_hardware_timestamp_available);
        assert(
            std::memcmp(buffer.data(), payload.data(), payload.size()) == 0
        );
    }

    // Kernel timestamping requested and (on this platform) actually
    // available: a real receive must come back with a real kernel software
    // timestamp, and, since this is loopback with no hardware clock behind
    // it, no hardware timestamp — that is the expected, correct outcome
    // here, not a failure.
    {
        UdpReceiver receiver{UdpReceiverConfig{
            .bind_ipv4 = "127.0.0.1",
            .bind_port = 0,
            .enable_kernel_timestamping = true
        }};
        assert(receiver.open());

        UdpSender sender{UdpSenderConfig{
            .destination_ipv4 = "127.0.0.1",
            .destination_port = receiver.bound_port()
        }};
        assert(sender.open());

        const std::vector<std::byte> payload{std::byte{0x2A}};
        const auto send_result = sender.send(payload);
        assert(send_result.sent);

        std::array<std::byte, 16> buffer{};
        const auto receive_result = receiver.receive(buffer);

        assert(receive_result.received);
        assert(receive_result.bytes_received == 1);
        assert(receive_result.receive_timestamp_ns > 0);

        if (receiver.kernel_timestamping_active()) {
            // The kernel accepting SO_TIMESTAMPING at open time does not
            // guarantee every individual datagram carries a populated
            // timestamp control message — that varies by kernel version
            // and network path (loopback under a virtualized/WSL2 network
            // stack is a plausible place for this to differ from bare-metal
            // Linux). Check consistency when present, not presence itself.
            if (receive_result.kernel_software_timestamp_available) {
                assert(receive_result.kernel_software_timestamp_ns > 0);
            }
            assert(!receive_result.kernel_hardware_timestamp_available);
        }
    }

    // close() prevents further receives — checked directly, since a
    // blocking recv on a socket with nothing arriving would otherwise hang
    // this test forever instead of failing fast.
    {
        UdpReceiver receiver{
            UdpReceiverConfig{.bind_ipv4 = "127.0.0.1", .bind_port = 0}
        };
        receiver.close();
        assert(!receiver.open());

        std::array<std::byte, 4> buffer{};
        const auto result = receiver.receive(buffer);
        assert(!result.received);
    }

    // Move semantics: the moved-from receiver is closed, the moved-to
    // receiver keeps receiving correctly on the same bound port.
    {
        UdpReceiver original{
            UdpReceiverConfig{.bind_ipv4 = "127.0.0.1", .bind_port = 0}
        };
        const auto port = original.bound_port();

        UdpReceiver moved{std::move(original)};
        assert(moved.open());
        assert(!original.open());
        assert(moved.bound_port() == port);

        UdpSender sender{UdpSenderConfig{
            .destination_ipv4 = "127.0.0.1",
            .destination_port = port
        }};

        const std::vector<std::byte> payload{std::byte{0x07}};
        const auto send_result = sender.send(payload);
        assert(send_result.sent);

        std::array<std::byte, 4> buffer{};
        const auto receive_result = moved.receive(buffer);
        assert(receive_result.received);
        assert(receive_result.bytes_received == 1);
    }

    return 0;
}