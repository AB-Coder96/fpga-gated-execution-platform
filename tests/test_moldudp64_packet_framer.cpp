#include "fgep/gen/itch_message_builder.hpp"
#include "fgep/gen/moldudp64_packet_framer.hpp"
#include "fgep/itch/itch_decode.hpp"
#include "fgep/moldudp64/moldudp64_decode.hpp"

#include <cassert>
#include <cstddef>
#include <variant>
#include <vector>

namespace {

[[nodiscard]] fgep::moldudp64::Session session_from_text(const char* text) {
    fgep::moldudp64::Session session{};
    session.fill(std::byte{' '});

    std::size_t index = 0;

    while (index < session.size() && text[index] != '\0') {
        session[index] = static_cast<std::byte>(
            static_cast<unsigned char>(text[index])
        );
        ++index;
    }

    return session;
}


[[nodiscard]] bool market_events_match(
    const fgep::itch::Message& original,
    const fgep::itch::Message& decoded
) {
    using fgep::itch::AddOrderNoMpidMessage;
    using fgep::itch::OrderCancelMessage;

    if (std::holds_alternative<AddOrderNoMpidMessage>(original)) {
        if (!std::holds_alternative<AddOrderNoMpidMessage>(decoded)) {
            return false;
        }

        const auto& original_add = std::get<AddOrderNoMpidMessage>(original);
        const auto& decoded_add = std::get<AddOrderNoMpidMessage>(decoded);

        return original_add.header.stock_locate
                == decoded_add.header.stock_locate
            && original_add.header.timestamp_ns
                == decoded_add.header.timestamp_ns
            && original_add.order_reference_number
                == decoded_add.order_reference_number
            && original_add.side == decoded_add.side
            && original_add.shares == decoded_add.shares
            && original_add.stock == decoded_add.stock
            && original_add.price == decoded_add.price;
    }

    if (std::holds_alternative<OrderCancelMessage>(original)) {
        if (!std::holds_alternative<OrderCancelMessage>(decoded)) {
            return false;
        }

        const auto& original_cancel = std::get<OrderCancelMessage>(original);
        const auto& decoded_cancel = std::get<OrderCancelMessage>(decoded);

        return original_cancel.header.stock_locate
                == decoded_cancel.header.stock_locate
            && original_cancel.order_reference_number
                == decoded_cancel.order_reference_number
            && original_cancel.cancelled_shares
                == decoded_cancel.cancelled_shares;
    }

    return false;
}

} // namespace

int main() {
    using namespace fgep::gen;
    using namespace fgep::moldudp64;

    const auto session = session_from_text("SESSION001");

    // Frame a batch of real ITCH messages from commit 1's builder, decode
    // the framed bytes back with the real decoders, and confirm every
    // message survives the round trip unchanged.
    {
        ItchMessageBuilder builder{};
        MoldUdp64PacketFramer framer{
            MoldUdp64PacketFramerConfig{
                .session = session,
                .first_sequence_number = 1
            }
        };

        std::vector<fgep::itch::Message> originals{};
        originals.push_back(builder.next_market_event());
        originals.push_back(builder.next_market_event());
        originals.push_back(builder.next_market_event());

        const auto framed = framer.frame_data_packet(originals);
        assert(!framed.empty());
        assert(framer.next_sequence_number() == 4);

        const auto decoded_packet = decode_downstream_packet(framed);
        assert(decoded_packet.ok());
        assert(decoded_packet.value.session == session);
        assert(decoded_packet.value.first_sequence_number == 1);
        assert(decoded_packet.value.messages.size() == 3);

        for (std::size_t index = 0; index < originals.size(); ++index) {
            assert(
                decoded_packet.value.messages[index].sequence_number
                    == 1 + index
            );

            const auto decoded_message = fgep::itch::decode_message(
                decoded_packet.value.messages[index].payload
            );
            assert(decoded_message.ok());
            assert(market_events_match(originals[index], decoded_message.value));
        }
    }

    // Sequence numbers keep advancing correctly across repeated calls.
    {
        ItchMessageBuilder builder{};
        MoldUdp64PacketFramer framer{
            MoldUdp64PacketFramerConfig{.session = session}
        };

        const auto first_batch = std::vector<fgep::itch::Message>{
            builder.next_market_event(),
            builder.next_market_event()
        };
        const auto first_framed = framer.frame_data_packet(first_batch);
        assert(!first_framed.empty());
        assert(framer.next_sequence_number() == 3);

        const auto second_batch = std::vector<fgep::itch::Message>{
            builder.next_market_event()
        };
        const auto second_framed = framer.frame_data_packet(second_batch);
        assert(!second_framed.empty());
        assert(framer.next_sequence_number() == 4);

        const auto decoded_second = decode_downstream_packet(second_framed);
        assert(decoded_second.ok());
        assert(decoded_second.value.first_sequence_number == 3);
    }

    // An empty batch frames nothing and does not advance the sequence
    // number.
    {
        MoldUdp64PacketFramer framer{
            MoldUdp64PacketFramerConfig{.session = session}
        };

        const auto framed = framer.frame_data_packet({});
        assert(framed.empty());
        assert(framer.next_sequence_number() == 1);
    }

    // Heartbeats decode as heartbeats and do not advance the sequence
    // number.
    {
        MoldUdp64PacketFramer framer{
            MoldUdp64PacketFramerConfig{
                .session = session,
                .first_sequence_number = 7
            }
        };

        const auto heartbeat = framer.frame_heartbeat();
        assert(!heartbeat.empty());
        assert(framer.next_sequence_number() == 7);

        const auto decoded = decode_downstream_packet(heartbeat);
        assert(decoded.ok());
        assert(decoded.value.kind == PacketKind::heartbeat);
        assert(decoded.value.first_sequence_number == 7);
    }

    // reset() restores the configured first sequence number.
    {
        MoldUdp64PacketFramer framer{
            MoldUdp64PacketFramerConfig{
                .session = session,
                .first_sequence_number = 5
            }
        };

        ItchMessageBuilder builder{};
        const auto batch = std::vector<fgep::itch::Message>{
            builder.next_market_event()
        };
        static_cast<void>(framer.frame_data_packet(batch));
        assert(framer.next_sequence_number() == 6);

        framer.reset();
        assert(framer.next_sequence_number() == 5);
    }

    return 0;
}