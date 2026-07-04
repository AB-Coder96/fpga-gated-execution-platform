#include "fgep/gen/moldudp64_packet_framer.hpp"

#include "fgep/itch/itch_encode.hpp"
#include "fgep/moldudp64/moldudp64_encode.hpp"

#include <limits>
#include <utility>
#include <variant>

namespace fgep::gen {
namespace {

[[nodiscard]] std::size_t message_wire_length(
    const itch::Message& message
) noexcept {
    return std::visit(
        [](const auto& concrete_message) {
            return itch::wire_length(concrete_message);
        },
        message
    );
}

} // namespace

MoldUdp64PacketFramer::MoldUdp64PacketFramer()
    : MoldUdp64PacketFramer{MoldUdp64PacketFramerConfig{}} {
}

MoldUdp64PacketFramer::MoldUdp64PacketFramer(
    MoldUdp64PacketFramerConfig config
)
    : config_{config} {
    if (config_.first_sequence_number == 0) {
        config_.first_sequence_number = 1;
    }

    next_sequence_number_ = config_.first_sequence_number;
}

std::vector<std::byte> MoldUdp64PacketFramer::frame_data_packet(
    std::span<const itch::Message> messages
) {
    if (messages.empty()) {
        return {};
    }

    if (messages.size()
        > static_cast<std::size_t>(
            std::numeric_limits<moldudp64::MessageCount>::max()
        )) {
        return {};
    }

    std::vector<std::vector<std::byte>> encoded_messages{};
    encoded_messages.reserve(messages.size());

    for (const auto& message : messages) {
        std::vector<std::byte> encoded(message_wire_length(message));

        if (itch::encode_message(encoded, message) != ErrorCode::ok) {
            return {};
        }

        encoded_messages.push_back(std::move(encoded));
    }

    std::vector<moldudp64::MessageBlock> blocks{};
    blocks.reserve(encoded_messages.size());

    for (std::size_t index = 0; index < encoded_messages.size(); ++index) {
        blocks.push_back(moldudp64::MessageBlock{
            .sequence_number = next_sequence_number_
                + static_cast<moldudp64::SequenceNumber>(index),
            .payload = encoded_messages[index]
        });
    }

    const moldudp64::DownstreamPacket packet{
        .session = config_.session,
        .first_sequence_number = next_sequence_number_,
        .message_count = static_cast<moldudp64::MessageCount>(blocks.size()),
        .kind = moldudp64::PacketKind::data,
        .messages = std::move(blocks)
    };

    std::vector<std::byte> framed(
        moldudp64::downstream_packet_encoded_length(packet)
    );

    if (moldudp64::encode_downstream_packet(framed, packet) != ErrorCode::ok) {
        return {};
    }

    next_sequence_number_ += static_cast<moldudp64::SequenceNumber>(
        messages.size()
    );

    return framed;
}

std::vector<std::byte> MoldUdp64PacketFramer::frame_heartbeat() const {
    const moldudp64::DownstreamPacket packet{
        .session = config_.session,
        .first_sequence_number = next_sequence_number_,
        .message_count = moldudp64::heartbeat_message_count,
        .kind = moldudp64::PacketKind::heartbeat,
        .messages = {}
    };

    std::vector<std::byte> framed(moldudp64::length_downstream_header);

    if (moldudp64::encode_downstream_packet(framed, packet) != ErrorCode::ok) {
        return {};
    }

    return framed;
}

moldudp64::SequenceNumber MoldUdp64PacketFramer::next_sequence_number()
    const noexcept {
    return next_sequence_number_;
}

void MoldUdp64PacketFramer::reset() noexcept {
    next_sequence_number_ = config_.first_sequence_number;
}

} // namespace fgep::gen