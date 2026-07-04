#pragma once

#include "fgep/itch/itch_wire_messages.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace fgep::gen {

// -----------------------------------------------------------------------------
// Synthetic ITCH message builder
// -----------------------------------------------------------------------------


struct ItchSymbolConfig {
    std::string_view symbol{"AAPL"};
    itch::Price4 base_price{1'000'000};
    itch::Price4 price_step{100};
};

struct ItchMessageBuilderConfig {
    std::vector<ItchSymbolConfig> symbols{};
    itch::StockLocate first_stock_locate{1};
    itch::OrderReferenceNumber first_order_reference_number{1};
    itch::Shares min_shares{100};
    itch::Shares max_shares{1'000};
    itch::TimestampNs first_timestamp_ns{0};
    itch::TimestampNs timestamp_step_ns{1'000};
    std::size_t hot_symbol_count{1};
    std::uint8_t hot_symbol_percent{80};
    std::uint8_t cancel_percent{10};
    bool alternate_sides{true};
};

class ItchMessageBuilder {
public:
    ItchMessageBuilder();

    explicit ItchMessageBuilder(ItchMessageBuilderConfig config);


    [[nodiscard]] std::vector<itch::Message> session_open_messages() const;

    [[nodiscard]] itch::Message next_market_event();

    [[nodiscard]] std::size_t generated_count() const noexcept;

    [[nodiscard]] std::size_t symbol_count() const noexcept;

    [[nodiscard]] itch::StockLocate stock_locate_at(
        std::size_t symbol_index
    ) const noexcept;

    void reset() noexcept;

private:
    struct SymbolState {
        ItchSymbolConfig config{};
        itch::StockLocate stock_locate{};
        std::vector<itch::OrderReferenceNumber> resting_orders{};
    };

    ItchMessageBuilderConfig config_{};
    std::vector<SymbolState> symbol_states_{};
    itch::OrderReferenceNumber next_order_reference_number_{1};
    std::size_t generated_count_{};

    [[nodiscard]] std::size_t choose_symbol_index() const noexcept;
    [[nodiscard]] itch::Shares choose_shares() const noexcept;
    [[nodiscard]] itch::Price4 choose_price(
        const SymbolState& symbol_state,
        itch::Side side
    ) const noexcept;
    [[nodiscard]] itch::Side choose_side() const noexcept;
    [[nodiscard]] itch::TimestampNs current_timestamp_ns() const noexcept;
    [[nodiscard]] bool should_cancel(
        const SymbolState& symbol_state
    ) const noexcept;

    [[nodiscard]] itch::Header make_header(
        itch::StockLocate stock_locate
    ) const noexcept;

    [[nodiscard]] itch::Message build_add_order(SymbolState& symbol_state);

    [[nodiscard]] itch::Message build_cancel(SymbolState& symbol_state);
};

} // namespace fgep::gen