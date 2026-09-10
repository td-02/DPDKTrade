#include <cassert>
#include <cstddef>
#include <cstdint>

#include <dpdktrade/book/order_book.hpp>
#include <dpdktrade/engine/dpdktrade_engine.hpp>
#include <dpdktrade/risk/guard.hpp>
#include <dpdktrade/strategy/imbalance.hpp>
#include <dpdktrade/wire/frame.hpp>

int main()
{
    using namespace dpdktrade;

    static_assert(sizeof(wire::MarketFrame) == 62);
    static_assert(sizeof(wire::OrderFrame) == 62);
    static_assert(sizeof(book::OrderBook::Level) == 16);

    wire::MarketFrame market_frame{};
    market_frame.ethertype = wire::ETHERTYPE_MARKET;
    market_frame.payload[0] = 0U;
    market_frame.payload[1] = 0xE8U;
    market_frame.payload[2] = 0x03U;
    market_frame.payload[9] = 0xC8U;
    market_frame.payload[10] = 0x00U;

    const wire::MarketFrame* decoded = wire::decode_market(std::span<const std::byte, sizeof(wire::MarketFrame)>(
        reinterpret_cast<const std::byte*>(&market_frame), sizeof(wire::MarketFrame)));
    assert(decoded == reinterpret_cast<const wire::MarketFrame*>(&market_frame));

    wire::OrderFrame order_frame{};
    order_frame.ethertype = wire::ETHERTYPE_ORDER;
    order_frame.payload[0] = 1U;
    order_frame.payload[1] = 0x34U;
    order_frame.payload[2] = 0x12U;

    const auto encoded = wire::encode_order(order_frame);
    assert(encoded.size() == sizeof(wire::OrderFrame));
    assert(static_cast<std::uint8_t>(encoded[0]) == static_cast<std::uint8_t>(order_frame.ethertype & 0xFFU));

    book::OrderBook order_book{};
    order_book.apply(book::OrderBook::Side::Bid, 101U, 50U);
    order_book.apply(book::OrderBook::Side::Bid, 102U, 70U);
    order_book.apply(book::OrderBook::Side::Ask, 103U, 60U);
    order_book.apply(book::OrderBook::Side::Ask, 104U, 80U);

    assert(order_book.best_bid().price == 102U);
    assert(order_book.best_bid().quantity == 70U);
    assert(order_book.best_ask().price == 103U);
    assert(order_book.best_ask().quantity == 60U);
    assert(order_book.bid_pressure() == 120U);
    assert(order_book.ask_pressure() == 140U);

    book::OrderBook sorted_book{};
    sorted_book.apply(book::OrderBook::Side::Bid, 100U, 10U);
    sorted_book.apply(book::OrderBook::Side::Bid, 105U, 20U);
    sorted_book.apply(book::OrderBook::Side::Bid, 103U, 30U);
    sorted_book.apply(book::OrderBook::Side::Ask, 120U, 11U);
    sorted_book.apply(book::OrderBook::Side::Ask, 115U, 21U);
    sorted_book.apply(book::OrderBook::Side::Ask, 118U, 31U);

    assert(sorted_book.best_bid().price == 105U);
    assert(sorted_book.best_bid().quantity == 20U);
    assert(sorted_book.best_ask().price == 115U);
    assert(sorted_book.best_ask().quantity == 21U);

    book::OrderBook empty_book{};
    assert(empty_book.best_bid().price == 0U);
    assert(empty_book.best_bid().quantity == 0U);
    assert(empty_book.best_ask().price == 0U);
    assert(empty_book.best_ask().quantity == 0U);

    book::OrderBook capped_book{};
    for (std::uint64_t index = 0; index < book::OrderBook::depth; ++index)
    {
        capped_book.apply(book::OrderBook::Side::Bid, 100U + index, 1U + index);
    }
    capped_book.apply(book::OrderBook::Side::Bid, 999U, 77U);
    assert(capped_book.best_bid().price == 999U);
    assert(capped_book.best_bid().quantity == 77U);
    assert(capped_book.bid_pressure() == 131U);

    capped_book.apply(book::OrderBook::Side::Bid, 999U, 0U);
    assert(capped_book.best_bid().price == 109U);
    assert(capped_book.best_bid().quantity == 10U);
    assert(capped_book.bid_pressure() == 54U);

    book::OrderBook buy_book{};
    buy_book.apply(book::OrderBook::Side::Bid, 100U, 220U);
    buy_book.apply(book::OrderBook::Side::Ask, 101U, 100U);
    assert(strategy::imbalance_signal(buy_book) == strategy::Signal::BUY);

    book::OrderBook sell_book{};
    sell_book.apply(book::OrderBook::Side::Bid, 100U, 100U);
    sell_book.apply(book::OrderBook::Side::Ask, 101U, 220U);
    assert(strategy::imbalance_signal(sell_book) == strategy::Signal::SELL);

    risk::RiskGuard risk_guard{{1000, 100000U}};
    assert(risk_guard.check_and_update(100, 10U));
    assert(!risk_guard.check_and_update(1000, 100U));
    assert(risk_guard.position() == 100);
    assert(risk_guard.notional() == 1000U);

    engine::DpdkTradeEngine engine{book::OrderBook{}, risk::RiskGuard{{1000, 100000U}}};

    wire::MarketFrame engine_market{};
    engine_market.ethertype = wire::ETHERTYPE_MARKET;
    engine_market.payload[0] = 0U;
    engine_market.payload[1] = 0x64U;
    engine_market.payload[9] = 0x96U;

    const auto engine_order = engine.on_market(engine_market);
    assert(engine_order.has_value());
    assert(engine_order->ethertype == wire::ETHERTYPE_ORDER);
    assert(engine_order->payload[1] == 0x64U);
    assert(engine_order->payload[9] == 0x96U);
    assert(engine.stats().total == 1U);

    wire::MarketFrame invalid_market{};
    invalid_market.ethertype = 0xFFFFU;
    const auto invalid_result = engine.on_market(invalid_market);
    assert(!invalid_result.has_value());
    assert(engine.stats().invalid == 1U);

    return 0;
}
