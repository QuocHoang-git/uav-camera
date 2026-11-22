#ifndef __UAV_MANAGER_STATE_MACHINE_HPP
#define __UAV_MANAGER_STATE_MACHINE_HPP

#include <cstdint>
#include <string_view>
#include <functional>
#include <chrono>

#include "manager/types.hpp"
#include "protocol/proto_system.h"

namespace uav::manager {

/**
 * @brief System Finite State Machine (FSM)
 *
 * Enforces valid state transitions and coordinates high-level operational
 * modes of the UAV Camera vision payload (STANDBY, STREAMING, RECORDING, ERROR, etc.)
 */
class StateMachine {
public:
    using StateChangeHandler = std::function<void(proto_sys_state_t old_state,
                                                 proto_sys_state_t new_state,
                                                 uint16_t reason_code)>;

    StateMachine();
    ~StateMachine() = default;

    /**
     * @brief Check whether a transition from the current state to target_state is allowed
     */
    bool canTransitionTo(proto_sys_state_t target_state) const;

    /**
     * @brief Execute a state transition
     *
     * @param target_state Desired new state
     * @param reason_code  Identifier explaining the reason for transition
     * @param force        If true, bypasses guard checks (used for emergency failsafe)
     * @return PROTO_OK if transition succeeded, or error code
     */
    proto_status_t transitionTo(proto_sys_state_t target_state,
                                uint16_t reason_code = 0,
                                bool force = false);

    /**
     * @brief Register a callback invoked whenever the state successfully transitions
     */
    void setStateChangeHandler(StateChangeHandler handler);

    /**
     * @brief Get active state
     */
    proto_sys_state_t getCurrentState() const noexcept { return current_state_; }

    /**
     * @brief Get previous state
     */
    proto_sys_state_t getPreviousState() const noexcept { return previous_state_; }

    /**
     * @brief Seconds spent in the current state
     */
    uint32_t getStateUptimeSec() const;

    /**
     * @brief Convenient query flags
     */
    bool isStreaming() const noexcept {
        return (current_state_ == SYS_STATE_STREAMING || current_state_ == SYS_STATE_STREAM_RECORD);
    }

    bool isRecording() const noexcept {
        return (current_state_ == SYS_STATE_RECORDING || current_state_ == SYS_STATE_STREAM_RECORD);
    }

    bool isError() const noexcept {
        return (current_state_ == SYS_STATE_ERROR);
    }

    /**
     * @brief Convert current state to string
     */
    std::string_view getCurrentStateStr() const;

    /**
     * @brief Populate a protocol response payload for PROTO_CMD_SYS_GET_STATE
     */
    proto_sys_get_state_resp_t getStatusResponse() const;

private:
    proto_sys_state_t current_state_  = SYS_STATE_INIT;
    proto_sys_state_t previous_state_ = SYS_STATE_UNKNOWN;

    std::chrono::steady_clock::time_point state_enter_time_;
    StateChangeHandler on_state_changed_;
};

} // namespace uav::manager

#endif // __UAV_MANAGER_STATE_MACHINE_HPP
