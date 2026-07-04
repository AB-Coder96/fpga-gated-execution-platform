#include "fgep/gen/itch_message_builder.hpp"

#include <algorithm>
#include <array>
#include <utility>

namespace fgep::gen {
namespace {

template <std::size_t Size>
[[nodiscard]] std::array<char, Size> fixed_ascii_from_text(
    std::string_view text
) noexcept {
    std::array<char, Size> value{};
    value.fill(' ');

    const auto copy_count = std::min(Size, text.size());

    for (std::size_t index = 0; index < copy_count; ++index) {
        value[index] = text[index];
    }

    return value;
}

[[nodiscard]] std::size_t clamp_hot_count(
    std::size_t hot_symbol_count,
    std::size_t symbol_count
) noexcept {
    if (symbol_count == 0) {
        return 0;
    }

    if (hot_symbol_count == 0) {
        return 1;
    }

    return std::min(hot_symbol_count, symbol_count);
}

[[nodiscard]] std::uint8_t clamp_percent(std::uint8_t percent) noexcept {
    return percent > 100U ? 100U : percent;
}

} // namespace

ItchMessageBuilder::ItchMessageBuilder()
    : ItchMessageBuilder{ItchMessageBuilderConfig{}} {
}

ItchMessageBuilder::ItchMessageBuilder(ItchMessageBuilderConfig config)
    : config_{std::move(config)},
      next_order_reference_number_{config_.first_order_reference_number} {
    if (config_.symbols.empty()) {
        config_.symbols.push_back(ItchSymbolConfig{});
    }

    if (config_.min_shares == 0) {
        config_.min_shares = 1;
    }

    if (config_.max_shares < config_.min_shares) {
        config_.max_shares = config_.min_shares;
    }

    config_.hot_symbol_count = clamp_hot_count(
        config_.hot_symbol_count,
        config_.symbols.size()
    );
    config_.hot_symbol_percent = clamp_percent(config_.hot_symbol_percent);
    config_.cancel_percent = clamp_percent(config_.cancel_percent);

    symbol_states_.reserve(config_.symbols.size());

    for (std::size_t index = 0; index < config_.symbols.size(); ++index) {
        symbol_states_.push_back(SymbolState{
            .config = config_.symbols[index],
            .stock_locate = static_cast<itch::StockLocate>(
                config_.first_stock_locate + index
            ),
            .resting_orders = {}
        });
    }
}

std::vector<itch::Message> ItchMessageBuilder::session_open_messages() const {
    std::vector<itch::Message> messages{};
    messages.reserve(symbol_states_.size());

    for (const auto& symbol_state : symbol_states_) {
        itch::StockDirectoryMessage message{};
        message.header = make_header(symbol_state.stock_locate);
        message.stock = fixed_ascii_from_text<8>(symbol_state.config.symbol);
        message.market_category = 'Q';
        message.financial_status_indicator = 'N';
        message.round_lot_size = 100;
        message.round_lots_only = 'N';
        message.issue_classification = 'C';
        message.issue_sub_type = fixed_ascii_from_text<2>("");
        message.authenticity = 'P';
        message.short_sale_threshold_indicator = 'N';
        message.ipo_flag = 'N';
        message.luld_reference_price_tier = ' ';
        message.etp_flag = 'N';
        message.etp_leverage_factor = 0;
        message.inverse_indicator = 'N';

        messages.push_back(message);
    }

    return messages;
}

itch::Message ItchMessageBuilder::next_market_event() {
    ++generated_count_;

    const auto symbol_index = choose_symbol_index();
    auto& symbol_state = symbol_states_[symbol_index];

    if (should_cancel(symbol_state)) {
        return build_cancel(symbol_state);
    }

    return build_add_order(symbol_state);
}

std::size_t ItchMessageBuilder::generated_count() const noexcept {
    return generated_count_;
}

std::size_t ItchMessageBuilder::symbol_count() const noexcept {
    return symbol_states_.size();
}

itch::StockLocate ItchMessageBuilder::stock_locate_at(
    std::size_t symbol_index
) const noexcept {
    if (symbol_index >= symbol_states_.size()) {
        return 0;
    }

    return symbol_states_[symbol_index].stock_locate;
}

void ItchMessageBuilder::reset() noexcept {
    next_order_reference_number_ = config_.first_order_reference_number;
    generated_count_ = 0;

    for (auto& symbol_state : symbol_states_) {
        symbol_state.resting_orders.clear();
    }
}

std::size_t ItchMessageBuilder::choose_symbol_index() const noexcept {
    const auto symbol_count_value = symbol_states_.size();

    if (symbol_count_value <= 1) {
        return 0;
    }

    const auto bucket = static_cast<std::uint8_t>(generated_count_ % 100U);

    if (bucket < config_.hot_symbol_percent) {
        return generated_count_ % config_.hot_symbol_count;
    }

    const auto cold_count = symbol_count_value - config_.hot_symbol_count;

    if (cold_count == 0) {
        return generated_count_ % config_.hot_symbol_count;
    }

    return config_.hot_symbol_count + (generated_count_ % cold_count);
}

itch::Shares ItchMessageBuilder::choose_shares() const noexcept {
    const auto range = config_.max_shares - config_.min_shares + 1U;
    const auto offset = static_cast<itch::Shares>(generated_count_ % range);

    return config_.min_shares + offset;
}

itch::Price4 ItchMessageBuilder::choose_price(
    const SymbolState& symbol_state,
    itch::Side side
) const noexcept {
    const auto& price_config = symbol_state.config;
    const auto wobble = static_cast<itch::Price4>(
        (generated_count_ % 10U) * price_config.price_step
    );
    const auto half_spread = price_config.price_step * 5U;

    if (side == itch::Side::sell) {
        return price_config.base_price + half_spread + wobble;
    }

    const auto below = half_spread + wobble;

    if (below >= price_config.base_price) {
        return price_config.price_step > 0 ? price_config.price_step : 1U;
    }

    return price_config.base_price - below;
}

itch::Side ItchMessageBuilder::choose_side() const noexcept {
    if (!config_.alternate_sides) {
        return itch::Side::buy;
    }

    return (generated_count_ % 2U) == 0U ? itch::Side::buy : itch::Side::sell;
}

itch::TimestampNs ItchMessageBuilder::current_timestamp_ns() const noexcept {
    const auto raw = config_.first_timestamp_ns
        + (static_cast<itch::TimestampNs>(generated_count_)
            * config_.timestamp_step_ns);

    return raw % itch::nanoseconds_per_day;
}

bool ItchMessageBuilder::should_cancel(
    const SymbolState& symbol_state
) const noexcept {
    if (symbol_state.resting_orders.empty()) {
        return false;
    }

    // Deliberately a different transform of generated_count_ than the one
    // choose_symbol_index() uses, so the cancel decision does not track the
    // hot/cold symbol decision one-for-one.
    const auto bucket = static_cast<std::uint8_t>(
        (generated_count_ * 7U + 3U) % 100U
    );

    return bucket < config_.cancel_percent;
}

itch::Header ItchMessageBuilder::make_header(
    itch::StockLocate stock_locate
) const noexcept {
    return itch::Header{
        .stock_locate = stock_locate,
        .tracking_number = 0,
        .timestamp_ns = current_timestamp_ns()
    };
}

itch::Message ItchMessageBuilder::build_add_order(SymbolState& symbol_state) {
    const auto side = choose_side();
    const auto order_reference_number = next_order_reference_number_;
    ++next_order_reference_number_;

    symbol_state.resting_orders.push_back(order_reference_number);

    itch::AddOrderNoMpidMessage message{};
    message.header = make_header(symbol_state.stock_locate);
    message.order_reference_number = order_reference_number;
    message.side = side;
    message.shares = choose_shares();
    message.stock = fixed_ascii_from_text<8>(symbol_state.config.symbol);
    message.price = choose_price(symbol_state, side);

    return message;
}

itch::Message ItchMessageBuilder::build_cancel(SymbolState& symbol_state) {
    const auto order_reference_number = symbol_state.resting_orders.front();
    symbol_state.resting_orders.erase(symbol_state.resting_orders.begin());

    itch::OrderCancelMessage message{};
    message.header = make_header(symbol_state.stock_locate);
    message.order_reference_number = order_reference_number;
    message.cancelled_shares = choose_shares();

    return message;
}

} // namespace fgep::gen