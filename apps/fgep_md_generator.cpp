#include "fgep/core/time.hpp"
#include "fgep/gen/itch_message_builder.hpp"
#include "fgep/gen/moldudp64_packet_framer.hpp"
#include "fgep/net/udp_sender.hpp"

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

volatile std::sig_atomic_t g_stop_requested = 0;

void handle_stop_signal(int /*signal_number*/) {
    g_stop_requested = 1;
}

void install_signal_handlers() {
    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);
}

[[nodiscard]] fgep::moldudp64::Session session_from_text(
    const char* text
) noexcept {
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

struct GeneratorConfig {
    std::string destination_ipv4{"127.0.0.1"};
    std::uint16_t destination_port{30001};
    std::uint64_t messages_per_second{1'000};
    std::uint64_t message_count{10'000};
};

} // namespace

int main() {
    install_signal_handlers();

    const GeneratorConfig config{};

    fgep::gen::ItchMessageBuilder builder{fgep::gen::ItchMessageBuilderConfig{
        .symbols = {
            fgep::gen::ItchSymbolConfig{
                .symbol = "AAPL", .base_price = 1'000'000, .price_step = 100
            },
            fgep::gen::ItchSymbolConfig{
                .symbol = "MSFT", .base_price = 2'000'000, .price_step = 100
            },
            fgep::gen::ItchSymbolConfig{
                .symbol = "TSLA", .base_price = 3'000'000, .price_step = 100
            }
        },
        .hot_symbol_count = 1
    }};

    fgep::gen::MoldUdp64PacketFramer framer{
        fgep::gen::MoldUdp64PacketFramerConfig{
            .session = session_from_text("FGEPGEN01")
        }
    };

    fgep::net::UdpSender sender{fgep::net::UdpSenderConfig{
        .destination_ipv4 = config.destination_ipv4,
        .destination_port = config.destination_port
    }};

    if (!sender.open()) {
        std::cerr << "fgep_md_generator: could not open UDP socket to "
                  << config.destination_ipv4 << ':' << config.destination_port
                  << '\n';
        return 1;
    }

    std::cout << "fgep_md_generator: sending to " << config.destination_ipv4
              << ':' << config.destination_port
              << " (" << builder.symbol_count() << " symbols, "
              << config.messages_per_second << " msg/s target)\n";

    const auto open_messages = builder.session_open_messages();
    std::uint64_t open_messages_sent = 0;

    for (const auto& message : open_messages) {
        const std::vector<fgep::itch::Message> batch{message};
        const auto framed = framer.frame_data_packet(batch);

        if (framed.empty()) {
            std::cerr
                << "fgep_md_generator: failed to frame session-open message\n";
            continue;
        }

        const auto result = sender.send(framed);

        if (result.sent) {
            ++open_messages_sent;
        }
    }

    std::cout << "fgep_md_generator: sent " << open_messages_sent << "/"
              << open_messages.size() << " session-open messages\n";

    const auto interval = config.messages_per_second > 0
        ? std::chrono::nanoseconds{
              1'000'000'000ULL / config.messages_per_second
          }
        : std::chrono::nanoseconds{0};

    std::uint64_t messages_sent = 0;
    std::uint64_t send_failures = 0;
    std::uint64_t bytes_sent = 0;

    const auto run_start_ns = fgep::now_ns();

    while (g_stop_requested == 0 && messages_sent < config.message_count) {
        const auto event = builder.next_market_event();
        const std::vector<fgep::itch::Message> batch{event};
        const auto framed = framer.frame_data_packet(batch);

        if (framed.empty()) {
            ++send_failures;
            continue;
        }

        const auto result = sender.send(framed);

        if (result.sent) {
            ++messages_sent;
            bytes_sent += result.bytes_sent;
        } else {
            ++send_failures;
        }

        if (interval.count() > 0) {
            std::this_thread::sleep_for(interval);
        }
    }

    const auto run_end_ns = fgep::now_ns();
    const auto elapsed_ns =
        run_end_ns > run_start_ns ? run_end_ns - run_start_ns : 0;
    const auto elapsed_seconds =
        static_cast<double>(elapsed_ns) / 1'000'000'000.0;

    std::cout << "fgep_md_generator: sent " << messages_sent << " messages, "
              << send_failures << " failures, "
              << bytes_sent << " bytes, "
              << elapsed_seconds << "s elapsed"
              << (g_stop_requested != 0 ? " (stopped by signal)" : "")
              << '\n';

    return 0;
}