#include "WebSocket.h"
#include <spdlog/spdlog.h>

WebSocketClient::WebSocketClient() {
}

WebSocketClient::~WebSocketClient() {
    Disconnect();
}

bool WebSocketClient::IsConnected() const {
    return mConnected;
}

bool WebSocketClient::Poll(std::string& outMessage) {
    std::lock_guard<std::mutex> lock(mQueueMutex);
    if (mIncomingQueue.empty()) {
        return false;
    }
    outMessage = mIncomingQueue.front();
    mIncomingQueue.pop();
    return true;
}

#ifdef __EMSCRIPTEN__
#include <emscripten.h>

// Connection state for the page's status pill (shell.html): 0 offline,
// 1 connecting, 2 connected.
static void NotifyPage(int state) {
    EM_ASM({ if (typeof window.SohNet !== 'undefined') window.SohNet.state($0); }, state);
}

bool WebSocketClient::Connect(const std::string& url) {
    if (mConnected) {
        return true;
    }
    // Reconnect: release the previous (closed or failed) socket first. Leaving
    // it registered leaked one handle per retry and let its late close event
    // flip the new connection's state.
    if (mSocket > 0) {
        emscripten_websocket_delete(mSocket);
        mSocket = 0;
    }

    EmscriptenWebSocketCreateAttributes attrs;
    attrs.url = url.c_str();
    attrs.protocols = nullptr;
    attrs.createOnMainThread = EM_TRUE;

    mSocket = emscripten_websocket_new(&attrs);
    if (mSocket <= 0) {
        SPDLOG_ERROR("[WebSocket] Failed to create WebSocket to {}", url);
        return false;
    }

    emscripten_websocket_set_onopen_callback(mSocket, this, OnOpen);
    emscripten_websocket_set_onmessage_callback(mSocket, this, OnMessage);
    emscripten_websocket_set_onclose_callback(mSocket, this, OnClose);
    emscripten_websocket_set_onerror_callback(mSocket, this, OnError);

    SPDLOG_INFO("[WebSocket] Connecting to {}", url);
    NotifyPage(1);
    return true;
}

void WebSocketClient::Disconnect() {
    if (mSocket > 0) {
        emscripten_websocket_close(mSocket, 1000, "disconnect");
        emscripten_websocket_delete(mSocket);
        mSocket = 0;
    }
    mConnected = false;
    NotifyPage(0);
}

void WebSocketClient::Send(const std::string& data) {
    if (!mConnected || mSocket <= 0) {
        return;
    }
    emscripten_websocket_send_utf8_text(mSocket, data.c_str());
}

EM_BOOL WebSocketClient::OnOpen(int eventType, const EmscriptenWebSocketOpenEvent* event, void* userData) {
    auto* self = static_cast<WebSocketClient*>(userData);
    if (event->socket != self->mSocket) {
        return EM_TRUE; // stale socket from an earlier attempt
    }
    self->mConnected = true;
    SPDLOG_INFO("[WebSocket] Connected!");
    NotifyPage(2);
    if (self->mOnConnect) {
        self->mOnConnect();
    }
    return EM_TRUE;
}

EM_BOOL WebSocketClient::OnMessage(int eventType, const EmscriptenWebSocketMessageEvent* event, void* userData) {
    auto* self = static_cast<WebSocketClient*>(userData);
    if (event->socket != self->mSocket) {
        return EM_TRUE;
    }
    if (event->isText) {
        std::string msg((const char*)event->data, event->numBytes);
        std::lock_guard<std::mutex> lock(self->mQueueMutex);
        self->mIncomingQueue.push(msg);
    }
    return EM_TRUE;
}

EM_BOOL WebSocketClient::OnClose(int eventType, const EmscriptenWebSocketCloseEvent* event, void* userData) {
    auto* self = static_cast<WebSocketClient*>(userData);
    if (event->socket != self->mSocket) {
        return EM_TRUE;
    }
    self->mConnected = false;
    std::string reason = event->reason;
    SPDLOG_INFO("[WebSocket] Disconnected (code={}, reason={})", event->code, reason);
    NotifyPage(0);
    if (self->mOnDisconnect) {
        self->mOnDisconnect();
    }
    return EM_TRUE;
}

EM_BOOL WebSocketClient::OnError(int eventType, const EmscriptenWebSocketErrorEvent* event, void* userData) {
    auto* self = static_cast<WebSocketClient*>(userData);
    SPDLOG_ERROR("[WebSocket] Error occurred");
    return EM_TRUE;
}

#else
// Native WebSocket implementation using a simple TCP-based approach
// For now, provide stub implementations that can be replaced with a real library

bool WebSocketClient::Connect(const std::string& url) {
    SPDLOG_WARN("[WebSocket] Native WebSocket not yet implemented, using stub");
    // TODO: Implement using a native WebSocket library (e.g., libwebsockets, boost.beast)
    return false;
}

void WebSocketClient::Disconnect() {
    mConnected = false;
}

void WebSocketClient::Send(const std::string& data) {
    // Stub
}

#endif // __EMSCRIPTEN__
