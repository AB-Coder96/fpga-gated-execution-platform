#pragma once

#include "fgep/itch/itch_wire_messages.hpp"
#include "fgep/moldudp64/moldudp64_packet.hpp"

#include <cstddef>
#include <span>
#include <vector>

namespace fgep::gen {

// -----------------------------------------------------------------------------
// MoldUDP64 packet framer for generated ITCH messages
// -----------------------------------------------------------------------------
//
// Takes fgep::itch::Message values (from ItchMessageBuilder or anywhere
// else) and produces fully framed MoldUDP64 downstream packets: encodes each
// message to ITCH wire bytes, packs them behind one MoldUDP64 header at the
// framer's current sequence number, and returns the ready-to-send UDP
// payload. This is where fgep_md_generator gets the bytes it hands to a
// socket — this class does not know about sockets or timestamps.
//
// Owns the running sequence number so repeated calls produce a correctly
// sequenced session without the caller having to track it.

struct MoldUdp64PacketFramerConfig {
    moldudp64::Session session{};
    moldudp64::SequenceNumber first_sequence_number{1};
};

class MoldUdp64PacketFramer {
public:
    MoldUdp64PacketFramer();

    explicit MoldUdp64PacketFramer(MoldUdp64PacketFramerConfig config);

    // Encodes every message in `messages` and frames them into a single
    // MoldUDP64 data packet starting at the framer's current sequence
    // number. Advances the sequence number by messages.size() on success.
    //
    // Returns an empty buffer, and leaves the sequence number unchanged, if
    // `messages` is empty, exceeds the wire message-count limit, or any
    // message fails to encode.
    [[nodiscard]] std::vector<std::byte> frame_data_packet(
        std::span<const itch::Message> messages
    );

    // A heartbeat packet at the framer's current sequence number. Heartbeats
    // are not part of the message stream, so this does not advance the
    // sequence number.
    [[nodiscard]] std::vector<std::byte> frame_heartbeat() const;

    [[nodiscard]] moldudp64::SequenceNumber next_sequence_number()
        const noexcept;

    void reset() noexcept;

private:
    MoldUdp64PacketFramerConfig config_{};
    moldudp64::SequenceNumber next_sequence_number_{1};
};

} // namespace fgep::gen