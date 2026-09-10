#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

#include <dpdktrade/book/order_book.hpp>
#include <dpdktrade/risk/guard.hpp>
#include <dpdktrade/strategy/imbalance.hpp>
#include <dpdktrade/wire/frame.hpp>

namespace dpdktrade::engine
{
class DpdkTradeEngine final
{
public:
    struct Statistics final
    {
        std::uint64_t total = 0;
        std::uint64_t buy = 0;
        std::uint64_t sell = 0;
        std::uint64_t no_signal = 0;
        std::uint64_t risk_reject = 0;
        std::uint64_t invalid = 0;
    };

    constexpr DpdkTradeEngine(book::OrderBook order_book, risk::RiskGuard risk_guard) noexcept
        : order_book_{order_book}
        , risk_guard_{risk_guard}
    {
    }

    [[nodiscard]] constexpr const Statistics& stats() const noexcept
    {
        return stats_;
    }

    [[nodiscard]] constexpr const book::OrderBook& order_book() const noexcept
    {
        return order_book_;
    }

    // Pipeline:
    // 1. Validate the market frame ethertype.
    // 2. Update the fixed-size order book.
    // 3. Generate a signal from current book state.
    // 4. Run risk checks before any order leaves the engine.
    // 5. Encode the order frame into a deterministic fixed-size wire image.
    //
    // The method returns std::nullopt on invalid input, no signal, or risk rejection.
    // That keeps the hot path free of heap allocations and avoids dynamic containers.
    [[nodiscard]] constexpr std::optional<wire::OrderFrame> on_market(const wire::MarketFrame& market_frame) noexcept
    {
        ++stats_.total;

        if (market_frame.ethertype != wire::ETHERTYPE_MARKET) [[unlikely]]
        {
            ++stats_.invalid;
            return std::nullopt;
        }

        const auto side = static_cast<book::OrderBook::Side>(market_frame.payload[0] & 0x01U);
        const std::uint64_t price = read_u64(market_frame.payload, 1);
        const std::uint64_t quantity = read_u64(market_frame.payload, 9);

        order_book_.apply(side, price, quantity);

        const strategy::Signal signal = strategy::imbalance_signal(order_book_);
        if (signal == strategy::Signal::NO_SIGNAL) [[likely]]
        {
            ++stats_.no_signal;
            return std::nullopt;
        }

        const bool is_buy = signal == strategy::Signal::BUY;
        if (is_buy)
        {
            ++stats_.buy;
        }
        else
        {
            ++stats_.sell;
        }

        const std::int64_t signed_quantity = is_buy ? static_cast<std::int64_t>(quantity)
                                                    : -static_cast<std::int64_t>(quantity);

        if (!risk_guard_.check_and_update(signed_quantity, price)) [[unlikely]]
        {
            ++stats_.risk_reject;
            return std::nullopt;
        }

        wire::OrderFrame order_frame{};
        order_frame.ethertype = wire::ETHERTYPE_ORDER;
        order_frame.payload[0] = is_buy ? 1U : 2U;
        write_u64(order_frame.payload, 1, price);
        write_u64(order_frame.payload, 9, quantity);
        return order_frame;
    }

private:
    book::OrderBook order_book_{};
    risk::RiskGuard risk_guard_;
    Statistics stats_{};

    [[nodiscard]] static constexpr std::uint64_t read_u64(const std::uint8_t* bytes, std::size_t offset) noexcept
    {
        const std::uint8_t* const field = bytes + offset;
        return static_cast<std::uint64_t>(field[0]) | (static_cast<std::uint64_t>(field[1]) << 8U) |
               (static_cast<std::uint64_t>(field[2]) << 16U) | (static_cast<std::uint64_t>(field[3]) << 24U) |
               (static_cast<std::uint64_t>(field[4]) << 32U) | (static_cast<std::uint64_t>(field[5]) << 40U) |
               (static_cast<std::uint64_t>(field[6]) << 48U) | (static_cast<std::uint64_t>(field[7]) << 56U);
    }

    static constexpr void write_u64(std::uint8_t* bytes, std::size_t offset, std::uint64_t value) noexcept
    {
        std::uint8_t* const field = bytes + offset;
        field[0] = static_cast<std::uint8_t>(value & 0xFFU);
        field[1] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
        field[2] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
        field[3] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
        field[4] = static_cast<std::uint8_t>((value >> 32U) & 0xFFU);
        field[5] = static_cast<std::uint8_t>((value >> 40U) & 0xFFU);
        field[6] = static_cast<std::uint8_t>((value >> 48U) & 0xFFU);
        field[7] = static_cast<std::uint8_t>((value >> 56U) & 0xFFU);
    }
};
} // namespace dpdktrade::engine
