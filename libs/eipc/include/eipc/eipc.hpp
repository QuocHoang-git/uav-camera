#ifndef __EIPC_HPP
#define __EIPC_HPP

#include "eipc/eipc.h"

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace eipc {

/**
 * @brief C++ Encapsulation for eipc_message_t
 */
class Message {
public:
    explicit Message(eipc_frame_type_t type = EIPC_FRAME_TYPE_DATA) {
        eipc_message_init(&raw_);
        setType(type);
    }

    Message(eipc_frame_type_t type, const void* data, uint32_t size) {
        eipc_message_init(&raw_);
        setType(type);
        if (data && size > 0) {
            setData(data, size);
        }
    }

    Message(eipc_frame_type_t type, std::string_view str)
        : Message(type, str.data(), static_cast<uint32_t>(str.size())) {}

    Message(eipc_frame_type_t type, const std::string& str)
        : Message(type, str.data(), static_cast<uint32_t>(str.size())) {}

    Message(eipc_frame_type_t type, const char* str)
        : Message(type, str ? std::string_view(str) : std::string_view{}) {}

    Message(eipc_frame_type_t type, const std::vector<uint8_t>& bytes)
        : Message(type, bytes.data(), static_cast<uint32_t>(bytes.size())) {}

    explicit Message(const eipc_message_t& raw) : raw_(raw) {}

    void setType(eipc_frame_type_t type) {
        eipc_message_set_type(&raw_, type);
    }

    void setData(const void* data, uint32_t size) {
        eipc_message_set_data(&raw_, const_cast<void*>(data), size);
    }

    void setFds(int* fds, int count) {
        eipc_message_set_fds(&raw_, fds, count);
    }

    void setFds(const std::vector<int>& fds) {
        eipc_message_set_fds(&raw_, const_cast<int*>(fds.data()), static_cast<int>(fds.size()));
    }

    eipc_frame_type_t type() const { return raw_.type; }
    const uint8_t* data() const { return raw_.data; }
    uint32_t size() const { return raw_.size; }
    int numFds() const { return raw_.num_fds; }
    const int* fds() const { return raw_.fds; }

    std::string_view asStringView() const {
        if (!raw_.data || raw_.size == 0) return {};
        return std::string_view(reinterpret_cast<const char*>(raw_.data), raw_.size);
    }

    std::string asString() const {
        return std::string(asStringView());
    }

    bool hasFds() const {
        return eipc_message_has_fds(&raw_);
    }

    bool isValid() const {
        return eipc_message_is_valid(&raw_);
    }

    const eipc_message_t& raw() const { return raw_; }
    eipc_message_t& raw() { return raw_; }

private:
    eipc_message_t raw_{};
};

/**
 * @brief RAII C++ Wrapper for EIPC Context (Broker or Client)
 */
class Context {
public:
    using ConnectHandler    = std::function<void(int peer_id)>;
    using DisconnectHandler = std::function<void(int peer_id)>;
    using MessageHandler    = std::function<void(int peer_id, const Message& msg)>;

    /**
     * @brief Factory: Tạo Broker instance
     */
    static std::unique_ptr<Context> createBroker(const std::string& socket_path) {
        return std::unique_ptr<Context>(new Context(socket_path, EIPC_ROLE_BROKER));
    }

    /**
     * @brief Factory: Tạo Client instance
     */
    static std::unique_ptr<Context> createClient(const std::string& socket_path) {
        return std::unique_ptr<Context>(new Context(socket_path, EIPC_ROLE_CLIENT));
    }

    ~Context() {
        destroy();
    }

    // Không cho phép copy để tránh duplicate ownership của C context
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // Cho phép Move
    Context(Context&& other) noexcept {
        moveFrom(std::move(other));
    }

    Context& operator=(Context&& other) noexcept {
        if (this != &other) {
            destroy();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // Đăng ký C++ Lambda Callbacks
    void onConnect(ConnectHandler handler)       { on_connect_ = std::move(handler); }
    void onDisconnect(DisconnectHandler handler) { on_disconnect_ = std::move(handler); }
    void onMessage(MessageHandler handler)       { on_msg_ = std::move(handler); }

    eipc_status_t init() {
        if (!ctx_) return EIPC_ERR_INVAL;
        return eipc_init(ctx_);
    }

    eipc_status_t connect() {
        if (!ctx_) return EIPC_ERR_INVAL;
        return eipc_connect(ctx_);
    }

    eipc_status_t loop(uint32_t timeout_ms = 100) {
        if (!ctx_) return EIPC_ERR_INVAL;
        return eipc_loop(ctx_, timeout_ms);
    }

    eipc_status_t sendMessage(int peer_id, const Message& msg) {
        if (!ctx_) return EIPC_ERR_INVAL;
        return eipc_send_message(ctx_, peer_id, &msg.raw());
    }

    eipc_status_t sendMessage(int peer_id, eipc_frame_type_t type, std::string_view text) {
        Message msg(type, text);
        return sendMessage(peer_id, msg);
    }

    eipc_status_t createDirectLink(int peer_id1, int peer_id2) {
        if (!ctx_) return EIPC_ERR_INVAL;
        return eipc_create_direct_link(ctx_, peer_id1, peer_id2);
    }

    int getEpollFd() const {
        return ctx_ ? eipc_get_epollfd(ctx_) : -1;
    }

    eipc_state_t getState() const {
        return ctx_ ? eipc_get_state(ctx_) : EIPC_STATE_INVALID;
    }

    bool isConnected() const {
        return getState() == EIPC_STATE_CONNECTED;
    }

    eipc_role_t getRole() const {
        return role_;
    }

    const std::string& getSocketPath() const {
        return socket_path_;
    }

    eipc_context_t* raw() const {
        return ctx_;
    }

private:
    Context(const std::string& path, eipc_role_t role)
        : socket_path_(path), role_(role) {

        eipc_config_t config;
        memset(&config, 0, sizeof(config));
        config.socket_path = socket_path_.c_str();
        config.role = role_;
        config.callbacks.userdata = this;
        config.callbacks.on_connect = &Context::trampolineConnect;
        config.callbacks.on_disconnect = &Context::trampolineDisconnect;
        config.callbacks.on_msg = &Context::trampolineMsg;

        ctx_ = eipc_create(&config);
        if (!ctx_) {
            throw std::runtime_error("[EIPC C++] Failed to create eipc_context");
        }
    }

    void destroy() {
        if (ctx_) {
            eipc_destroy(ctx_);
            ctx_ = nullptr;
        }
    }

    void moveFrom(Context&& other) {
        socket_path_ = std::move(other.socket_path_);
        role_ = other.role_;
        ctx_ = other.ctx_;
        on_connect_ = std::move(other.on_connect_);
        on_disconnect_ = std::move(other.on_disconnect_);
        on_msg_ = std::move(other.on_msg_);
        other.ctx_ = nullptr;
    }

    static void trampolineConnect(eipc_context_t* /*ctx*/, int peer_id, void* userdata) {
        auto* self = static_cast<Context*>(userdata);
        if (self && self->on_connect_) {
            self->on_connect_(peer_id);
        }
    }

    static void trampolineDisconnect(eipc_context_t* /*ctx*/, int peer_id, void* userdata) {
        auto* self = static_cast<Context*>(userdata);
        if (self && self->on_disconnect_) {
            self->on_disconnect_(peer_id);
        }
    }

    static void trampolineMsg(eipc_context_t* /*ctx*/, int peer_id, const eipc_message_t* data, void* userdata) {
        auto* self = static_cast<Context*>(userdata);
        if (self && self->on_msg_ && data) {
            Message msg(*data);
            self->on_msg_(peer_id, msg);
        }
    }

    std::string socket_path_;
    eipc_role_t role_{EIPC_ROLE_CLIENT};
    eipc_context_t* ctx_{nullptr};

    ConnectHandler    on_connect_;
    DisconnectHandler on_disconnect_;
    MessageHandler    on_msg_;
};

} // namespace eipc

#endif // __EIPC_HPP
