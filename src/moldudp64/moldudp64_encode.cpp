#include "fgep/moldudp64/moldudp64_encode.hpp"

#include "fgep/wire/byte_io.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace fgep::moldudp64 {

namespace {

[[nodiscard]] ErrorCode write_session(
    std::span<std::byte> bytes,
    std::size_t offset,
    const Session& session
) noexcept {
    if (!wire::has_range(bytes, offset, length_session)) {
        return ErrorCode::parse_error;
    }

    std::copy_n(
        session.begin(),
        length_session,
        bytes.begin() + static_cast<std::ptrdiff_t>(offset)
    );

    return ErrorCode::ok;
}

} // namespace

ErrorCode encode_request_packet(
    std::span<std::byte> bytes,
    const RequestPacket& packet
) noexcept {
    if (bytes.size() != length_request_packet) {
        return ErrorCode::parse_error;
    }

    auto error = write_session(bytes, offset_request_session, packet.session);
    if (error != ErrorCode::ok) {
        return error;
    }

    error = wire::write_u64_be(
        bytes,
        offset_request_sequence_number,
        packet.sequence_number
    );
    if (error != ErrorCode::ok) {
        return error;
    }

    return wire::write_u16_be(
        bytes,
        offset_requested_message_count,
        packet.requested_message_count
    );
}

std::size_t downstream_packet_encoded_length(
    const DownstreamPacket& packet
) noexcept {
    if (packet.kind != PacketKind::data) {
        return length_downstream_header;
    }

    std::size_t total = length_downstream_header;

    for (const auto& message : packet.messages) {
        total += sizeof(MessageLength) + message.payload.size();
    }

    return total;
}

namespace {

[[nodiscard]] ErrorCode encode_downstream_header(
    std::span<std::byte> bytes,
    const DownstreamPacket& packet
) noexcept {
    auto error = write_session(bytes, offset_session, packet.session);
    if (error != ErrorCode::ok) {
        return error;
    }

    error = wire::write_u64_be(
        bytes,
        offset_sequence_number,
        packet.first_sequence_number
    );
    if (error != ErrorCode::ok) {
        return error;
    }

    return wire::write_u16_be(
        bytes,
        offset_message_count,
        packet.message_count
    );
}

} // namespace

ErrorCode encode_downstream_packet(
    std::span<std::byte> bytes,
    const DownstreamPacket& packet
) noexcept {
    if (bytes.size() != downstream_packet_encoded_length(packet)) {
        return ErrorCode::parse_error;
    }

    if (packet.kind != PacketKind::data) {
        if (!packet.messages.empty()) {
            return ErrorCode::invalid_argument;
        }

        if (packet.kind == PacketKind::heartbeat
            && packet.message_count != heartbeat_message_count) {
            return ErrorCode::invalid_argument;
        }

        if (packet.kind == PacketKind::end_of_session
            && packet.message_count != end_of_session_message_count) {
            return ErrorCode::invalid_argument;
        }

        return encode_downstream_header(bytes, packet);
    }

    if (packet.messages.size()
        != static_cast<std::size_t>(packet.message_count)) {
        return ErrorCode::invalid_argument;
    }

    auto error = encode_downstream_header(bytes, packet);
    if (error != ErrorCode::ok) {
        return error;
    }

    std::size_t offset = length_downstream_header;

    for (std::size_t index = 0; index < packet.messages.size(); ++index) {
        const auto& message = packet.messages[index];

        const auto expected_sequence_number = packet.first_sequence_number
            + static_cast<SequenceNumber>(index);

        if (message.sequence_number != expected_sequence_number) {
            return ErrorCode::invalid_argument;
        }

        if (message.payload.size()
            > static_cast<std::size_t>(
                std::numeric_limits<MessageLength>::max()
            )) {
            return ErrorCode::invalid_argument;
        }

        error = wire::write_u16_be(
            bytes,
            offset,
            static_cast<MessageLength>(message.payload.size())
        );
        if (error != ErrorCode::ok) {
            return error;
        }

        offset += sizeof(MessageLength);

        std::copy_n(
            message.payload.begin(),
            message.payload.size(),
            bytes.begin() + static_cast<std::ptrdiff_t>(offset)
        );

        offset += message.payload.size();
    }

    return ErrorCode::ok;
}

} // namespace fgep::moldudp64