// MIT License
// Copyright (c) 2026 dbjwhs

#include "stockticker.hpp"
#include <iostream>
#include <unordered_map>
#include <chrono>
#include <cmath>

using namespace song;
using namespace song::stockticker;

// Get current timestamp in milliseconds
static i64 now_ms() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
}

// Mock stock data
struct StockData {
    i64 base_price_cents;
    i64 prev_close_cents;
    i64 volume;
};

static const std::unordered_map<std::string, StockData> kStockDatabase = {
    {"AAPL", {17523, 17400, 50000000}},
    {"GOOGL", {14234, 14100, 20000000}},
    {"MSFT", {37845, 37700, 30000000}},
    {"AMZN", {17856, 17600, 40000000}},
    {"TSLA", {24567, 24000, 80000000}},
    {"META", {35678, 35400, 25000000}},
    {"NVDA", {87234, 86500, 35000000}},
    {"JPM", {18923, 18800, 15000000}},
};

// Stock ticker implementation
class StockTickerImpl : public IStockTicker {
public:
    Quote get_quote(const std::string& symbol) override {
        auto it = kStockDatabase.find(symbol);
        if (it == kStockDatabase.end()) {
            throw std::runtime_error("Unknown symbol: " + symbol);
        }

        const auto& data = it->second;
        Quote quote;
        quote.symbol = symbol;
        quote.price_cents = data.base_price_cents;
        quote.change_cents = data.base_price_cents - data.prev_close_cents;
        quote.volume = data.volume;
        quote.timestamp = now_ms();
        return quote;
    }

    std::vector<Quote> get_quotes(const std::vector<std::string>& symbols) override {
        std::vector<Quote> quotes;
        quotes.reserve(symbols.size());
        for (const auto& symbol : symbols) {
            quotes.push_back(get_quote(symbol));
        }
        return quotes;
    }

    std::vector<PricePoint> get_history(const std::string& symbol, i32 points) override {
        auto it = kStockDatabase.find(symbol);
        if (it == kStockDatabase.end()) {
            throw std::runtime_error("Unknown symbol: " + symbol);
        }

        // Generate mock historical data
        std::vector<PricePoint> history;
        history.reserve(points);

        i64 current_time = now_ms();
        i64 base_price = it->second.base_price_cents;
        i64 interval_ms = 60000;  // 1 minute intervals

        for (i32 i = points - 1; i >= 0; --i) {
            PricePoint point;
            point.timestamp = current_time - (i * interval_ms);
            // Add some variation to simulate price movement
            double variation = std::sin(i * 0.5) * 50;  // +/- 50 cents
            point.price_cents = base_price + static_cast<i64>(variation);
            point.volume = it->second.volume / points;
            history.push_back(point);
        }

        return history;
    }

    MarketStatus get_market_status() override {
        MarketStatus status;
        // Mock: market is open during "business hours"
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto* tm = std::localtime(&time_t_now);

        bool is_weekday = tm->tm_wday >= 1 && tm->tm_wday <= 5;
        bool is_market_hours = tm->tm_hour >= 9 && tm->tm_hour < 16;

        status.is_open = is_weekday && is_market_hours;
        status.next_open = now_ms() + 3600000;   // +1 hour (mock)
        status.next_close = now_ms() + 7200000;  // +2 hours (mock)
        status.message = status.is_open ? "Market is open for trading" : "Market is closed";

        return status;
    }

    bool symbol_exists(const std::string& symbol) override {
        return kStockDatabase.find(symbol) != kStockDatabase.end();
    }

    std::vector<std::string> list_symbols() override {
        std::vector<std::string> symbols;
        symbols.reserve(kStockDatabase.size());
        for (const auto& [symbol, _] : kStockDatabase) {
            symbols.push_back(symbol);
        }
        return symbols;
    }
};

// Global instance for dispatcher
static StockTickerImpl g_ticker;

// Dispatcher wrapper for ServiceRuntime
void stockticker_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_StockTicker(g_ticker, method_id, request, response);
}

int main() {
    std::cerr << "[stockticker_service] Starting...\n";

    ServiceRuntime runtime;

    // Register stock ticker service
    runtime.register_dispatcher(kService_StockTicker, stockticker_dispatcher);

    // Register all methods for capability exchange
    runtime.register_method(kService_StockTicker, kMethod_StockTicker_get_quote);
    runtime.register_method(kService_StockTicker, kMethod_StockTicker_get_quotes);
    runtime.register_method(kService_StockTicker, kMethod_StockTicker_get_history);
    runtime.register_method(kService_StockTicker, kMethod_StockTicker_get_market_status);
    runtime.register_method(kService_StockTicker, kMethod_StockTicker_symbol_exists);
    runtime.register_method(kService_StockTicker, kMethod_StockTicker_list_symbols);

    // Run the service
    runtime.run();

    return 0;
}
