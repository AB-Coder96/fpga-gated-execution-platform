#include "fgep/hardware/fpga_gate_reference.hpp"

#include <cstddef>
#include <cstdint>

namespace fgep::hardware {
namespace {

[[nodiscard]] bool valid_stock_locate(std::uint32_t stock_locate) noexcept {
    return stock_locate < SoftwareReferenceFpgaGate::max_stock_locates;
}

[[nodiscard]] bool packet_length_matches(
    const FpgaGateRequest& request
) noexcept {
    return request.metadata.packet_length == request.packet.size();
}

[[nodiscard]] std::uint64_t decision_timestamp(
    const FpgaGateRequest& request
) noexcept {
    if (request.metadata.receive_timestamp_ns == 0U) {
        return 0U;
    }

    return request.metadata.receive_timestamp_ns + 1U;
}

} // namespace

SoftwareReferenceFpgaGate::SoftwareReferenceFpgaGate() {
    set_all_stock_locates_enabled(true);
}

SoftwareReferenceFpgaGate::SoftwareReferenceFpgaGate(
    FpgaGateReferenceConfig config
)
    : config_{config} {
    set_all_stock_locates_enabled(true);
}

bool SoftwareReferenceFpgaGate::available() const noexcept {
    return true;
}

FpgaGateResult SoftwareReferenceFpgaGate::evaluate(
    const FpgaGateRequest& request
) noexcept {
    ++counters_.packets;

    auto make_result = [&](FpgaGateDecision decision, FpgaGateReason reason) {
        switch (decision) {
        case FpgaGateDecision::accept:
            ++counters_.accepted;
            break;
        case FpgaGateDecision::reject:
            ++counters_.rejected;
            break;
        case FpgaGateDecision::drop:
            ++counters_.dropped;
            break;
        case FpgaGateDecision::pass_to_software:
            ++counters_.passed_to_software;
            break;
        case FpgaGateDecision::unavailable:
            ++counters_.unavailable;
            break;
        }

        if (reason == FpgaGateReason::malformed_packet) {
            ++counters_.malformed;
        }

        return FpgaGateResult{
            .decision = decision,
            .reason = reason,
            .sequence_number = request.metadata.sequence_number,
            .receive_timestamp_ns = request.metadata.receive_timestamp_ns,
            .decision_timestamp_ns = decision_timestamp(request),
            .timestamp_source = FpgaTimestampSource::software_reference,
            .counters = counters_
        };
    };

    if (!config_.enabled) {
        return make_result(
            FpgaGateDecision::pass_to_software,
            FpgaGateReason::software_required
        );
    }

    if (request.packet.empty() || !packet_length_matches(request)) {
        return make_result(
            FpgaGateDecision::drop,
            FpgaGateReason::malformed_packet
        );
    }

    if (config_.detect_sequence_gaps
        && request.metadata.sequence_number != 0U) {
        if (expected_next_sequence_number_ != 0U
            && request.metadata.sequence_number
                != expected_next_sequence_number_) {
            expected_next_sequence_number_ =
                request.metadata.sequence_number + 1U;

            return make_result(
                FpgaGateDecision::reject,
                FpgaGateReason::sequence_gap
            );
        }

        expected_next_sequence_number_ =
            request.metadata.sequence_number + 1U;
    }

    if (config_.require_instrument_key
        && !request.metadata.has_instrument_key) {
        return make_result(
            FpgaGateDecision::pass_to_software,
            FpgaGateReason::config_miss
        );
    }

    if (config_.enforce_enabled_stock_locates
        && !stock_locate_enabled(request.metadata.stock_locate)) {
        return make_result(
            FpgaGateDecision::reject,
            FpgaGateReason::symbol_not_enabled
        );
    }

    if (config_.require_order_fields
        && !request.metadata.has_order_fields) {
        return make_result(
            FpgaGateDecision::pass_to_software,
            FpgaGateReason::software_required
        );
    }

    if (request.metadata.has_order_fields) {
        const bool quantity_ok =
            request.metadata.quantity >= config_.min_quantity
            && request.metadata.quantity <= config_.max_quantity;

        const bool price_ok =
            request.metadata.price >= config_.min_price
            && request.metadata.price <= config_.max_price;

        if (!quantity_ok || !price_ok) {
            return make_result(
                FpgaGateDecision::reject,
                FpgaGateReason::risk_reject
            );
        }
    }

    return make_result(
        FpgaGateDecision::accept,
        FpgaGateReason::none
    );
}

FpgaGateCounters SoftwareReferenceFpgaGate::counters() const noexcept {
    return counters_;
}

void SoftwareReferenceFpgaGate::reset_counters() noexcept {
    counters_ = FpgaGateCounters{};
}

void SoftwareReferenceFpgaGate::reset_sequence_tracking() noexcept {
    expected_next_sequence_number_ = 0U;
}

void SoftwareReferenceFpgaGate::set_config(
    FpgaGateReferenceConfig config
) noexcept {
    config_ = config;
}

const FpgaGateReferenceConfig& SoftwareReferenceFpgaGate::config()
    const noexcept {
    return config_;
}

void SoftwareReferenceFpgaGate::set_all_stock_locates_enabled(
    bool enabled
) noexcept {
    enabled_stock_locates_.fill(enabled);
}

void SoftwareReferenceFpgaGate::set_stock_locate_enabled(
    std::uint32_t stock_locate,
    bool enabled
) noexcept {
    if (!valid_stock_locate(stock_locate)) {
        return;
    }

    enabled_stock_locates_[stock_locate] = enabled;
}

bool SoftwareReferenceFpgaGate::stock_locate_enabled(
    std::uint32_t stock_locate
) const noexcept {
    if (!valid_stock_locate(stock_locate)) {
        return false;
    }

    return enabled_stock_locates_[stock_locate];
}

} // namespace fgep::hardware