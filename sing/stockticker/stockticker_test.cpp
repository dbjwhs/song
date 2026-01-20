// MIT License
// Copyright (c) 2026 dbjwhs

#include <gtest/gtest.h>
#include <song/song.hpp>
#include "stockticker.hpp"
#include <filesystem>
#include <algorithm>

using namespace song;
using namespace song::stockticker;

// Helper to get service path
static std::string get_service_path() {
    std::filesystem::path base = std::filesystem::current_path();

    std::vector<std::filesystem::path> candidates = {
        base / "sing" / "stockticker" / "sing_stockticker_service",
        base / "sing_stockticker_service",
        base / ".." / "sing" / "stockticker" / "sing_stockticker_service"
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return p.string();
        }
    }

    return (base / "sing" / "stockticker" / "sing_stockticker_service").string();
}

class StockTickerTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::string path = get_service_path();
        if (!std::filesystem::exists(path)) {
            GTEST_SKIP() << "StockTicker service not found at " << path;
        }

        proc_ = ServiceProcess::spawn(path.c_str());
        conn_ = std::make_unique<ServiceConnection>(&proc_);
    }

    void TearDown() override {
        conn_.reset();
        if (proc_.alive()) {
            proc_.terminate();
        }
    }

    ServiceProcess proc_;
    std::unique_ptr<ServiceConnection> conn_;
};

// =============================================================================
// Single Quote Tests
// =============================================================================

TEST_F(StockTickerTest, GetQuoteApple) {
    StockTickerProxy ticker(*conn_);

    Quote quote = ticker.get_quote("AAPL");

    EXPECT_EQ(quote.symbol, "AAPL");
    EXPECT_GT(quote.price_cents, 0);
    EXPECT_GT(quote.timestamp, 0);
    EXPECT_GT(quote.volume, 0);
}

TEST_F(StockTickerTest, GetQuoteGoogle) {
    StockTickerProxy ticker(*conn_);

    Quote quote = ticker.get_quote("GOOGL");

    EXPECT_EQ(quote.symbol, "GOOGL");
    EXPECT_GT(quote.price_cents, 0);
}

TEST_F(StockTickerTest, GetQuoteNvidia) {
    StockTickerProxy ticker(*conn_);

    Quote quote = ticker.get_quote("NVDA");

    EXPECT_EQ(quote.symbol, "NVDA");
    // NVDA should have highest price in our mock data
    EXPECT_GT(quote.price_cents, 80000);
}

// =============================================================================
// Batch Quote Tests
// =============================================================================

TEST_F(StockTickerTest, GetQuotesSingle) {
    StockTickerProxy ticker(*conn_);

    std::vector<std::string> symbols = {"AAPL"};
    auto quotes = ticker.get_quotes(symbols);

    ASSERT_EQ(quotes.size(), 1);
    EXPECT_EQ(quotes[0].symbol, "AAPL");
}

TEST_F(StockTickerTest, GetQuotesMultiple) {
    StockTickerProxy ticker(*conn_);

    std::vector<std::string> symbols = {"AAPL", "GOOGL", "MSFT"};
    auto quotes = ticker.get_quotes(symbols);

    ASSERT_EQ(quotes.size(), 3);

    // Find each symbol
    std::vector<std::string> returned_symbols;
    for (const auto& q : quotes) {
        returned_symbols.push_back(q.symbol);
    }

    EXPECT_TRUE(std::find(returned_symbols.begin(), returned_symbols.end(), "AAPL") != returned_symbols.end());
    EXPECT_TRUE(std::find(returned_symbols.begin(), returned_symbols.end(), "GOOGL") != returned_symbols.end());
    EXPECT_TRUE(std::find(returned_symbols.begin(), returned_symbols.end(), "MSFT") != returned_symbols.end());
}

TEST_F(StockTickerTest, GetQuotesAll) {
    StockTickerProxy ticker(*conn_);

    // First get all symbols
    auto symbols = ticker.list_symbols();
    ASSERT_GT(symbols.size(), 0);

    // Then get quotes for all of them
    auto quotes = ticker.get_quotes(symbols);
    EXPECT_EQ(quotes.size(), symbols.size());
}

// =============================================================================
// History Tests
// =============================================================================

TEST_F(StockTickerTest, GetHistorySingle) {
    StockTickerProxy ticker(*conn_);

    auto history = ticker.get_history("AAPL", 1);

    ASSERT_EQ(history.size(), 1);
    EXPECT_GT(history[0].price_cents, 0);
    EXPECT_GT(history[0].timestamp, 0);
}

TEST_F(StockTickerTest, GetHistoryMultiple) {
    StockTickerProxy ticker(*conn_);

    auto history = ticker.get_history("AAPL", 10);

    ASSERT_EQ(history.size(), 10);

    // Timestamps should be in ascending order
    for (size_t i = 1; i < history.size(); ++i) {
        EXPECT_GT(history[i].timestamp, history[i-1].timestamp);
    }
}

TEST_F(StockTickerTest, GetHistoryLarge) {
    StockTickerProxy ticker(*conn_);

    auto history = ticker.get_history("MSFT", 100);

    ASSERT_EQ(history.size(), 100);

    // All prices should be positive
    for (const auto& point : history) {
        EXPECT_GT(point.price_cents, 0);
    }
}

// =============================================================================
// Market Status Tests
// =============================================================================

TEST_F(StockTickerTest, GetMarketStatus) {
    StockTickerProxy ticker(*conn_);

    MarketStatus status = ticker.get_market_status();

    // Status message should be non-empty
    EXPECT_FALSE(status.message.empty());

    // Times should be in the future
    EXPECT_GT(status.next_open, 0);
    EXPECT_GT(status.next_close, 0);
}

// =============================================================================
// Symbol Validation Tests
// =============================================================================

TEST_F(StockTickerTest, SymbolExistsValid) {
    StockTickerProxy ticker(*conn_);

    EXPECT_TRUE(ticker.symbol_exists("AAPL"));
    EXPECT_TRUE(ticker.symbol_exists("GOOGL"));
    EXPECT_TRUE(ticker.symbol_exists("MSFT"));
}

TEST_F(StockTickerTest, SymbolExistsInvalid) {
    StockTickerProxy ticker(*conn_);

    EXPECT_FALSE(ticker.symbol_exists("INVALID"));
    EXPECT_FALSE(ticker.symbol_exists("FAKE"));
    EXPECT_FALSE(ticker.symbol_exists(""));
}

TEST_F(StockTickerTest, ListSymbols) {
    StockTickerProxy ticker(*conn_);

    auto symbols = ticker.list_symbols();

    // Should have at least some symbols
    EXPECT_GT(symbols.size(), 5);

    // Should include known symbols
    auto has_aapl = std::find(symbols.begin(), symbols.end(), "AAPL") != symbols.end();
    auto has_googl = std::find(symbols.begin(), symbols.end(), "GOOGL") != symbols.end();

    EXPECT_TRUE(has_aapl);
    EXPECT_TRUE(has_googl);
}

// =============================================================================
// Multiple Operations
// =============================================================================

TEST_F(StockTickerTest, MultipleOperations) {
    StockTickerProxy ticker(*conn_);

    // First check market status
    auto status = ticker.get_market_status();
    EXPECT_FALSE(status.message.empty());

    // List all symbols
    auto symbols = ticker.list_symbols();
    EXPECT_GT(symbols.size(), 0);

    // Get quotes for first 3 symbols
    std::vector<std::string> subset(symbols.begin(), symbols.begin() + std::min<size_t>(3, symbols.size()));
    auto quotes = ticker.get_quotes(subset);
    EXPECT_EQ(quotes.size(), subset.size());

    // Get history for first symbol
    if (!symbols.empty()) {
        auto history = ticker.get_history(symbols[0], 5);
        EXPECT_EQ(history.size(), 5);
    }
}

TEST_F(StockTickerTest, RapidFireQuotes) {
    StockTickerProxy ticker(*conn_);

    // Make many rapid calls
    for (int i = 0; i < 50; ++i) {
        Quote quote = ticker.get_quote("AAPL");
        EXPECT_EQ(quote.symbol, "AAPL");
    }
}
