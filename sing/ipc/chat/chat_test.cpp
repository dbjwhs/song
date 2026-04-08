// MIT License
// Copyright (c) 2026 dbjwhs

// Chat Service - Integration Tests
// Tests stateful server interactions and message history

#include <gtest/gtest.h>
#include <song/song.hpp>
#include "chat.hpp"
#include <unistd.h>

using namespace song;
using namespace song::chat;

class ChatTest : public ::testing::Test {
protected:
    ServiceProcess proc_;
    std::unique_ptr<ServiceConnection> conn_;
    std::unique_ptr<ChatProxy> chat_;

    void SetUp() override {
        // Find the service executable
        std::string path = "./sing_ipc_chat_service";
        if (access(path.c_str(), X_OK) != 0) {
            path = "./sing/ipc/chat/sing_ipc_chat_service";
        }

        proc_ = ServiceProcess::spawn(path.c_str());
        conn_ = std::make_unique<ServiceConnection>(&proc_);
        chat_ = std::make_unique<ChatProxy>(*conn_);

        // Clear any state from previous tests
        chat_->clear_history();
    }

    void TearDown() override {
        chat_.reset();
        conn_.reset();
    }
};

// ============================================================================
// Basic Send/Receive Tests
// ============================================================================

TEST_F(ChatTest, SendSingleMessage) {
    auto result = chat_->send("Alice", "Hello, world!");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message_id, 1);
    EXPECT_GT(result.timestamp, 0);
}

TEST_F(ChatTest, SendMultipleMessages) {
    auto r1 = chat_->send("Alice", "First message");
    auto r2 = chat_->send("Bob", "Second message");
    auto r3 = chat_->send("Alice", "Third message");

    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    EXPECT_TRUE(r3.success);
    EXPECT_EQ(r1.message_id, 1);
    EXPECT_EQ(r2.message_id, 2);
    EXPECT_EQ(r3.message_id, 3);
    // Timestamps should be monotonically increasing (or equal)
    EXPECT_LE(r1.timestamp, r2.timestamp);
    EXPECT_LE(r2.timestamp, r3.timestamp);
}

TEST_F(ChatTest, SendEmptyContent) {
    auto result = chat_->send("Alice", "");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message_id, 1);
}

TEST_F(ChatTest, SendUnicodeContent) {
    auto result = chat_->send("日本語", "こんにちは世界 🌍");

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.message_id, 1);
}

// ============================================================================
// History Tests
// ============================================================================

TEST_F(ChatTest, GetHistoryEmpty) {
    auto history = chat_->get_history(10);

    EXPECT_EQ(history.messages.size(), 0);
    EXPECT_EQ(history.total_count, 0);
    EXPECT_FALSE(history.has_more);
}

TEST_F(ChatTest, GetHistoryAllMessages) {
    chat_->send("Alice", "First");
    chat_->send("Bob", "Second");
    chat_->send("Charlie", "Third");

    auto history = chat_->get_history(10);

    EXPECT_EQ(history.messages.size(), 3);
    EXPECT_EQ(history.total_count, 3);
    EXPECT_FALSE(history.has_more);

    // Check messages are in chronological order
    EXPECT_EQ(history.messages[0].sender, "Alice");
    EXPECT_EQ(history.messages[0].content, "First");
    EXPECT_EQ(history.messages[1].sender, "Bob");
    EXPECT_EQ(history.messages[1].content, "Second");
    EXPECT_EQ(history.messages[2].sender, "Charlie");
    EXPECT_EQ(history.messages[2].content, "Third");
}

TEST_F(ChatTest, GetHistoryLimited) {
    chat_->send("Alice", "1");
    chat_->send("Bob", "2");
    chat_->send("Charlie", "3");
    chat_->send("Dave", "4");
    chat_->send("Eve", "5");

    auto history = chat_->get_history(3);

    EXPECT_EQ(history.messages.size(), 3);
    EXPECT_EQ(history.total_count, 5);
    EXPECT_TRUE(history.has_more);

    // Should return the last 3 messages
    EXPECT_EQ(history.messages[0].sender, "Charlie");
    EXPECT_EQ(history.messages[1].sender, "Dave");
    EXPECT_EQ(history.messages[2].sender, "Eve");
}

TEST_F(ChatTest, GetHistoryZeroCount) {
    chat_->send("Alice", "Test");

    auto history = chat_->get_history(0);

    EXPECT_EQ(history.messages.size(), 0);
    EXPECT_EQ(history.total_count, 1);
    EXPECT_TRUE(history.has_more);
}

// ============================================================================
// Pagination Tests
// ============================================================================

TEST_F(ChatTest, GetMessagesAfterExistingId) {
    auto r1 = chat_->send("Alice", "1");
    chat_->send("Bob", "2");
    chat_->send("Charlie", "3");
    chat_->send("Dave", "4");

    auto history = chat_->get_messages_after(r1.message_id, 10);

    EXPECT_EQ(history.messages.size(), 3);
    EXPECT_EQ(history.total_count, 4);
    EXPECT_FALSE(history.has_more);

    EXPECT_EQ(history.messages[0].sender, "Bob");
    EXPECT_EQ(history.messages[1].sender, "Charlie");
    EXPECT_EQ(history.messages[2].sender, "Dave");
}

TEST_F(ChatTest, GetMessagesAfterLimited) {
    auto r1 = chat_->send("Alice", "1");
    chat_->send("Bob", "2");
    chat_->send("Charlie", "3");
    chat_->send("Dave", "4");
    chat_->send("Eve", "5");

    auto history = chat_->get_messages_after(r1.message_id, 2);

    EXPECT_EQ(history.messages.size(), 2);
    EXPECT_TRUE(history.has_more);

    EXPECT_EQ(history.messages[0].sender, "Bob");
    EXPECT_EQ(history.messages[1].sender, "Charlie");
}

TEST_F(ChatTest, GetMessagesAfterNonexistentId) {
    chat_->send("Alice", "1");
    chat_->send("Bob", "2");

    auto history = chat_->get_messages_after(999, 10);

    EXPECT_EQ(history.messages.size(), 0);
    EXPECT_FALSE(history.has_more);
}

TEST_F(ChatTest, GetMessagesAfterLastMessage) {
    chat_->send("Alice", "1");
    auto r2 = chat_->send("Bob", "2");

    auto history = chat_->get_messages_after(r2.message_id, 10);

    EXPECT_EQ(history.messages.size(), 0);
    EXPECT_FALSE(history.has_more);
}

// ============================================================================
// Clear History Tests
// ============================================================================

TEST_F(ChatTest, ClearHistory) {
    chat_->send("Alice", "1");
    chat_->send("Bob", "2");
    chat_->send("Charlie", "3");

    i32 cleared = chat_->clear_history();

    EXPECT_EQ(cleared, 3);
    EXPECT_EQ(chat_->get_message_count(), 0);

    auto history = chat_->get_history(10);
    EXPECT_EQ(history.messages.size(), 0);
}

TEST_F(ChatTest, ClearHistoryResetsIdCounter) {
    chat_->send("Alice", "1");
    chat_->send("Bob", "2");
    chat_->clear_history();

    // New messages should start from ID 1 again
    auto result = chat_->send("Charlie", "New");
    EXPECT_EQ(result.message_id, 1);
}

TEST_F(ChatTest, ClearEmptyHistory) {
    i32 cleared = chat_->clear_history();
    EXPECT_EQ(cleared, 0);
}

// ============================================================================
// Message Count Tests
// ============================================================================

TEST_F(ChatTest, MessageCountEmpty) {
    EXPECT_EQ(chat_->get_message_count(), 0);
}

TEST_F(ChatTest, MessageCountIncreases) {
    EXPECT_EQ(chat_->get_message_count(), 0);
    chat_->send("Alice", "1");
    EXPECT_EQ(chat_->get_message_count(), 1);
    chat_->send("Bob", "2");
    EXPECT_EQ(chat_->get_message_count(), 2);
    chat_->send("Charlie", "3");
    EXPECT_EQ(chat_->get_message_count(), 3);
}

// ============================================================================
// Message Exists Tests
// ============================================================================

TEST_F(ChatTest, MessageExistsTrue) {
    auto r1 = chat_->send("Alice", "Test");

    EXPECT_TRUE(chat_->message_exists(r1.message_id));
}

TEST_F(ChatTest, MessageExistsFalse) {
    chat_->send("Alice", "Test");

    EXPECT_FALSE(chat_->message_exists(999));
}

TEST_F(ChatTest, MessageExistsAfterClear) {
    auto r1 = chat_->send("Alice", "Test");
    EXPECT_TRUE(chat_->message_exists(r1.message_id));

    chat_->clear_history();
    EXPECT_FALSE(chat_->message_exists(r1.message_id));
}

// ============================================================================
// State Persistence Tests (within same session)
// ============================================================================

TEST_F(ChatTest, StatePersistsAcrossOperations) {
    // Send messages
    auto r1 = chat_->send("Alice", "Hello");
    auto r2 = chat_->send("Bob", "Hi there");

    // Verify via different operations
    EXPECT_EQ(chat_->get_message_count(), 2);
    EXPECT_TRUE(chat_->message_exists(r1.message_id));
    EXPECT_TRUE(chat_->message_exists(r2.message_id));

    auto history = chat_->get_history(10);
    EXPECT_EQ(history.messages.size(), 2);

    // Send more messages
    chat_->send("Charlie", "Hey!");

    // Verify updated state
    EXPECT_EQ(chat_->get_message_count(), 3);
    history = chat_->get_history(10);
    EXPECT_EQ(history.messages.size(), 3);
}

// ============================================================================
// Message Content Integrity Tests
// ============================================================================

TEST_F(ChatTest, MessageContentPreserved) {
    std::string long_content = "This is a longer message with special characters: "
                               "!@#$%^&*()_+-=[]{}|;':\",./<>? and newlines\n\n"
                               "and tabs\t\tand unicode: café résumé naïve";

    chat_->send("TestUser", long_content);

    auto history = chat_->get_history(1);
    ASSERT_EQ(history.messages.size(), 1);
    EXPECT_EQ(history.messages[0].content, long_content);
    EXPECT_EQ(history.messages[0].sender, "TestUser");
}

TEST_F(ChatTest, MessageIdMonotonicallyIncreasing) {
    std::vector<i64> ids;
    for (int ndx = 0; ndx < 10; ++ndx) {
        auto result = chat_->send("User", "Message " + std::to_string(ndx));
        ids.push_back(result.message_id);
    }

    for (size_t i = 1; i < ids.size(); ++i) {
        EXPECT_GT(ids[i], ids[i - 1]);
    }
}
