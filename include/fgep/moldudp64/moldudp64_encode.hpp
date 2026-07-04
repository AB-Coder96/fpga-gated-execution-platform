#pragma once

#include "fgep/core/errors.hpp"
#include "fgep/moldudp64/moldudp64_packet.hpp"

#include <cstddef>
#include <span>

namespace fgep::moldudp64 {

[[nodiscard]] ErrorCode encode_request_packet(
    std::span<std::byte> bytes,
    const RequestPacket& packet
) noexcept;

// The exact byte length encode_downstream_packet() needs for `packet`.
// Heartbeat and end-of-session packets are always length_downstream_header;
// data packets add a 2-byte length prefix plus the payload for each message.
[[nodiscard]] std::size_t downstream_packet_encoded_length(
    const DownstreamPacket& packet
) noexcept;


//
// For heartbeat/end-of-session packets, packet.messages must be empty.
[[nodiscard]] ErrorCode encode_downstream_packet(
    std::span<std::byte> bytes,
    const DownstreamPacket& packet
) noexcept;
} // namespace fgep::moldudp64