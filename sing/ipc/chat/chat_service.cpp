// MIT License
// Copyright (c) 2026 dbjwhs

// Chat Service - Server Implementation
// Stateful chat service with message history

#include <song/song.hpp>
#include <song/logging.hpp>
#include "chat.hpp"
#include <vector>
#include <chrono>
#include <algorithm>
#include <string>

using namespace song;
using namespace song::chat;

// Get current timestamp in milliseconds
static i64 now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

class ChatImpl : public IChat {
    std::vector<Message> m_messages;
    i64 m_next_id = 1;

public:
    SendResult send(const std::string& sender, const std::string& content) override {
        i64 timestamp = now_ms();
        i64 msg_id = m_next_id++;

        Message msg;
        msg.id = msg_id;
        msg.sender = sender;
        msg.content = content;
        msg.timestamp = timestamp;
        m_messages.push_back(msg);

        SendResult result;
        result.success = true;
        result.message_id = msg_id;
        result.timestamp = timestamp;
        return result;
    }

    HistoryResult get_history(i32 count) override {
        HistoryResult result;
        result.total_count = static_cast<i32>(m_messages.size());

        if (count <= 0) {
            result.has_more = !m_messages.empty();
            return result;
        }

        size_t start = 0;
        if (m_messages.size() > static_cast<size_t>(count)) {
            start = m_messages.size() - count;
            result.has_more = true;
        } else {
            result.has_more = false;
        }

        for (size_t i = start; i < m_messages.size(); ++i) {
            result.messages.push_back(m_messages[i]);
        }

        return result;
    }

    HistoryResult get_messages_after(i64 after_id, i32 count) override {
        HistoryResult result;
        result.total_count = static_cast<i32>(m_messages.size());

        // Find starting position
        auto it = std::find_if(m_messages.begin(), m_messages.end(),
            [after_id](const Message& m) { return m.id == after_id; });

        if (it == m_messages.end()) {
            // Message not found, return empty
            result.has_more = false;
            return result;
        }

        // Start from the message after the found one
        ++it;

        i32 added = 0;
        while (it != m_messages.end() && added < count) {
            result.messages.push_back(*it);
            ++it;
            ++added;
        }

        result.has_more = (it != m_messages.end());
        return result;
    }

    i32 clear_history() override {
        i32 count = static_cast<i32>(m_messages.size());
        m_messages.clear();
        m_next_id = 1;
        return count;
    }

    i32 get_message_count() override {
        return static_cast<i32>(m_messages.size());
    }

    bool message_exists(i64 message_id) override {
        return std::any_of(m_messages.begin(), m_messages.end(),
            [message_id](const Message& m) { return m.id == message_id; });
    }
};

static ChatImpl g_chat;

void chat_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_Chat(g_chat, method_id, request, response);
}

int main() {
    Log::info("IPC Chat service starting");

    ServiceRuntime runtime;

    // Register chat service dispatcher
    runtime.register_dispatcher(kService_Chat, chat_dispatcher);

    // Register all methods for capability exchange
    runtime.register_method(kService_Chat, kMethod_Chat_send);
    runtime.register_method(kService_Chat, kMethod_Chat_get_history);
    runtime.register_method(kService_Chat, kMethod_Chat_get_messages_after);
    runtime.register_method(kService_Chat, kMethod_Chat_clear_history);
    runtime.register_method(kService_Chat, kMethod_Chat_get_message_count);
    runtime.register_method(kService_Chat, kMethod_Chat_message_exists);

    Log::debug("Service ready: " + std::to_string(runtime.service_count()) + " service(s), " +
               std::to_string(runtime.method_count()) + " method(s)");

    // Run the service
    runtime.run();

    return 0;
}
