#pragma once

#include "fgep/hardware/fpga_gate.hpp"
#include "fgep/hardware/fpga_gate_result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fgep::hardware {

struct FpgaGateReferenceConfig {
    bool enabled{true};

    bool require_instrument_key{true};
    bool require_order_fields{true};
    bool enforce_enabled_stock_locates{true};
    bool detect_sequence_gaps{true};

    std::uint32_t min_quantity{1U};
    std::uint32_t max_quantity{1'000'000U};

    std::uint32_t min_price{1U};
    std::uint32_t max_price{1'000'000'000U};
};

class SoftwareReferenceFpgaGate final : public FpgaGate {
public:
    SoftwareReferenceFpgaGate();
    explicit SoftwareReferenceFpgaGate(
        FpgaGateReferenceConfig config
    );

    [[nodiscard]] bool available() const noexcept override;

    [[nodiscard]] FpgaGateResult evaluate(
        const FpgaGateRequest& request
    ) noexcept override;

    [[nodiscard]] FpgaGateCounters counters() const noexcept override;

    void reset_counters() noexcept;
    void reset_sequence_tracking() noexcept;

    void set_config(FpgaGateReferenceConfig config) noexcept;

    [[nodiscard]] const FpgaGateReferenceConfig& config() const noexcept;

    void set_all_stock_locates_enabled(bool enabled) noexcept;

    void set_stock_locate_enabled(
        std::uint32_t stock_locate,
        bool enabled
    ) noexcept;

    [[nodiscard]] bool stock_locate_enabled(
        std::uint32_t stock_locate
    ) const noexcept;

private:
    static constexpr std::size_t max_stock_locates = 65'536U;

    FpgaGateReferenceConfig config_{};
    FpgaGateCounters counters_{};
    std::array<bool, max_stock_locates> enabled_stock_locates_{};
    std::uint64_t expected_next_sequence_number_{};
};

} // namespace fgep::hardware