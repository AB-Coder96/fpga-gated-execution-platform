#include "fgep/moldudp64/moldudp64_decode.hpp"
#include "fgep/moldudp64/moldudp64_encode.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <vector>

namespace {

[[nodiscard]] fgep::moldudp64::Session session_from_text(const char* text) {
    fgep::moldudp64::Session session{};

    for (std::size_t index = 0; index < session.size(); ++index) {
        session[index] = static_cast<std::byte>(
            static_cast<unsigned char>(text[index])
        );
    }

    return session;
}

[[nodiscard]] std::vector<std::byte> payload_from_text(const char* text) {
    std::vector<std::byte> bytes{};

    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        bytes.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(*cursor)
        ));
    }

    return bytes;
}

} // namespace

int main() {
    using namespace fgep;
    using namespace fgep::moldudp64;

    const auto session = session_from_text("SESSION001");

    // Data packet with two message blocks: encode, then decode, and confirm
    // the round trip is exact.
    {
        const auto payload_a = payload_from_text("hello");
        const auto payload_b = payload_from_text("world!");

        const std::vector<MessageBlock> messages{
            MessageBlock{.sequence_number = 100, .payload = payload_a},
            MessageBlock{.sequence_number = 101, .payload = payload_b}
        };

        const DownstreamPacket packet{
            .session = session,
            .first_sequence_number = 100,
            .message_count = 2,
            .kind = PacketKind::data,
            .messages = messages
        };

        const auto length = downstream_packet_encoded_length(packet);
        assert(length == length_downstream_header + 2 + 5 + 2 + 6);

        std::vector<std::byte> bytes(length);
        assert(encode_downstream_packet(bytes, packet) == ErrorCode::ok);

        const auto decoded = decode_downstream_packet(bytes);
        assert(decoded.ok());
        assert(decoded.value.session == session);
        assert(decoded.value.first_sequence_number == 100);
        assert(decoded.value.message_count == 2);
        assert(decoded.value.messages.size() == 2);
        assert(decoded.value.messages[0].sequence_number == 100);
        assert(decoded.value.messages[1].sequence_number == 101);

        assert(std::equal(
            decoded.value.messages[0].payload.begin(),
            decoded.value.messages[0].payload.end(),
            payload_a.begin(),
            payload_a.end()
        ));
        assert(std::equal(
            decoded.value.messages[1].payload.begin(),
            decoded.value.messages[1].payload.end(),
            payload_b.begin(),
            payload_b.end()
        ));
    }

    // Heartbeat and end-of-session packets: header only, no message blocks.
    {
        const DownstreamPacket heartbeat{
            .session = session,
            .first_sequence_number = 42,
            .message_count = heartbeat_message_count,
            .kind = PacketKind::heartbeat,
            .messages = {}
        };

        assert(
            downstream_packet_encoded_length(heartbeat)
                == length_downstream_header
        );

        std::vector<std::byte> bytes(length_downstream_header);
        assert(encode_downstream_packet(bytes, heartbeat) == ErrorCode::ok);

        const auto decoded = decode_downstream_packet(bytes);
        assert(decoded.ok());
        assert(decoded.value.kind == PacketKind::heartbeat);
        assert(decoded.value.messages.empty());
    }

    // Error cases: wrong buffer size, message_count/messages.size()
    // mismatch, and a MessageBlock whose sequence_number does not match its
    // implicit wire position must all be rejected.
    {
        const auto payload_a = payload_from_text("x");

        const DownstreamPacket packet{
            .session = session,
            .first_sequence_number = 1,
            .message_count = 1,
            .kind = PacketKind::data,
            .messages = {MessageBlock{.sequence_number = 1, .payload = payload_a}}
        };

        const auto correct_length = downstream_packet_encoded_length(packet);

        std::vector<std::byte> too_short(correct_length - 1);
        assert(
            encode_downstream_packet(too_short, packet)
                == ErrorCode::parse_error
        );

        const DownstreamPacket mismatched_count{
            .session = session,
            .first_sequence_number = 1,
            .message_count = 2,
            .kind = PacketKind::data,
            .messages = {MessageBlock{.sequence_number = 1, .payload = payload_a}}
        };

        std::vector<std::byte> buffer(
            downstream_packet_encoded_length(mismatched_count)
        );
        assert(
            encode_downstream_packet(buffer, mismatched_count)
                == ErrorCode::invalid_argument
        );

        const DownstreamPacket wrong_sequence{
            .session = session,
            .first_sequence_number = 1,
            .message_count = 1,
            .kind = PacketKind::data,
            .messages = {
                MessageBlock{.sequence_number = 999, .payload = payload_a}
            }
        };

        std::vector<std::byte> buffer2(
            downstream_packet_encoded_length(wrong_sequence)
        );
        assert(
            encode_downstream_packet(buffer2, wrong_sequence)
                == ErrorCode::invalid_argument
        );
    }

    return 0;
}