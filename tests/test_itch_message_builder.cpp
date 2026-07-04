#include "fgep/gen/itch_message_builder.hpp"
#include "fgep/wire/fixed_ascii.hpp"

#include <cassert>
#include <variant>

namespace {

[[nodiscard]] fgep::itch::StockSymbol symbol(const char* text) {
    const auto value = fgep::wire::make_fixed_ascii<8>(text);
    assert(value.ok());
    return value.value;
}

} // namespace

int main() {
    using namespace fgep::gen;
    using namespace fgep::itch;

    // Default config: one symbol, session open message, then a basic add
    // order flow.
    {
        ItchMessageBuilder builder{};

        const auto open_messages = builder.session_open_messages();
        assert(open_messages.size() == 1);
        assert(std::holds_alternative<StockDirectoryMessage>(open_messages[0]));

        const auto& directory = std::get<StockDirectoryMessage>(
            open_messages[0]
        );
        assert(directory.stock == symbol("AAPL"));
        assert(directory.header.stock_locate == 1);

        const auto first = builder.next_market_event();
        assert(std::holds_alternative<AddOrderNoMpidMessage>(first));

        const auto& first_add = std::get<AddOrderNoMpidMessage>(first);
        assert(first_add.header.stock_locate == 1);
        assert(first_add.order_reference_number == 1);
        assert(first_add.side == Side::sell);
        assert(first_add.shares == 101);
        assert(first_add.price == 1'000'600);
        assert(first_add.stock == symbol("AAPL"));

        assert(builder.generated_count() == 1);
        assert(builder.symbol_count() == 1);
        assert(builder.stock_locate_at(0) == 1);
    }

    // Multi-symbol config, mirroring bench::OrderCandidateGenerator's
    // hot/cold shape: with hot_symbol_percent == 100 every event lands on
    // one of the hot_symbol_count symbols, cycling in order.
    {
        ItchMessageBuilder builder{ItchMessageBuilderConfig{
            .symbols = {
                ItchSymbolConfig{
                    .symbol = "AAPL", .base_price = 1'000'000, .price_step = 100
                },
                ItchSymbolConfig{
                    .symbol = "MSFT", .base_price = 2'000'000, .price_step = 100
                },
                ItchSymbolConfig{
                    .symbol = "TSLA", .base_price = 3'000'000, .price_step = 100
                }
            },
            .first_stock_locate = 10,
            .hot_symbol_count = 2,
            .hot_symbol_percent = 100,
            .cancel_percent = 0
        }};

        assert(builder.symbol_count() == 3);
        assert(builder.stock_locate_at(0) == 10);
        assert(builder.stock_locate_at(1) == 11);
        assert(builder.stock_locate_at(2) == 12);

        const auto open_messages = builder.session_open_messages();
        assert(open_messages.size() == 3);

        const auto first = builder.next_market_event();
        const auto second = builder.next_market_event();
        const auto third = builder.next_market_event();

        const auto& first_add = std::get<AddOrderNoMpidMessage>(first);
        const auto& second_add = std::get<AddOrderNoMpidMessage>(second);
        const auto& third_add = std::get<AddOrderNoMpidMessage>(third);

        assert(first_add.header.stock_locate == 11);
        assert(second_add.header.stock_locate == 10);
        assert(third_add.header.stock_locate == 11);
    }

    // Cancel behavior: never cancels a symbol with no resting order, cancels
    // a real previously-added order reference number once one exists, and
    // does not cancel the same order twice.
    {
        ItchMessageBuilder builder{ItchMessageBuilderConfig{
            .symbols = {ItchSymbolConfig{.symbol = "AAPL"}},
            .cancel_percent = 100
        }};

        const auto first = builder.next_market_event();
        assert(std::holds_alternative<AddOrderNoMpidMessage>(first));
        const auto first_order_ref =
            std::get<AddOrderNoMpidMessage>(first).order_reference_number;

        const auto second = builder.next_market_event();
        assert(std::holds_alternative<OrderCancelMessage>(second));
        const auto& cancel = std::get<OrderCancelMessage>(second);
        assert(cancel.order_reference_number == first_order_ref);

        const auto third = builder.next_market_event();
        assert(std::holds_alternative<AddOrderNoMpidMessage>(third));
    }

    // reset() restores the order reference counter and the running message
    // count, and clears resting-order state.
    {
        ItchMessageBuilder builder{ItchMessageBuilderConfig{
            .symbols = {ItchSymbolConfig{.symbol = "AAPL"}},
            .cancel_percent = 0
        }};

        const auto before_reset = builder.next_market_event();
        const auto before_ref =
            std::get<AddOrderNoMpidMessage>(before_reset).order_reference_number;

        builder.reset();
        assert(builder.generated_count() == 0);

        const auto after_reset = builder.next_market_event();
        const auto after_ref =
            std::get<AddOrderNoMpidMessage>(after_reset).order_reference_number;

        assert(before_ref == after_ref);
    }

    return 0;
}