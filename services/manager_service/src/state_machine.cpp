#include "manager/state_machine.hpp"
#include <chrono>

namespace uav::manager {

StateMachine::StateMachine()
    : current_state_(SYS_STATE_INIT),
      previous_state_(SYS_STATE_UNKNOWN),
      state_enter_time_(std::chrono::steady_clock::now()) {}

bool StateMachine::canTransitionTo(proto_sys_state_t target_state) const {
    if (current_state_ == target_state) {
        return false;
    }

    switch (current_state_) {
        case SYS_STATE_INIT:
            /* From INIT, can only transition to STANDBY, ERROR, or SHUTDOWN */
            return (target_state == SYS_STATE_STANDBY ||
                    target_state == SYS_STATE_ERROR   ||
                    target_state == SYS_STATE_SHUTDOWN);

        case SYS_STATE_STANDBY:
            /* From STANDBY, any operational mode is reachable */
            return (target_state == SYS_STATE_STREAMING     ||
                    target_state == SYS_STATE_RECORDING     ||
                    target_state == SYS_STATE_STREAM_RECORD ||
                    target_state == SYS_STATE_CALIBRATION   ||
                    target_state == SYS_STATE_LOW_POWER     ||
                    target_state == SYS_STATE_ERROR         ||
                    target_state == SYS_STATE_SHUTDOWN);

        case SYS_STATE_STREAMING:
            return (target_state == SYS_STATE_STANDBY       ||
                    target_state == SYS_STATE_STREAM_RECORD ||
                    target_state == SYS_STATE_LOW_POWER     ||
                    target_state == SYS_STATE_ERROR         ||
                    target_state == SYS_STATE_SHUTDOWN);

        case SYS_STATE_RECORDING:
            return (target_state == SYS_STATE_STANDBY       ||
                    target_state == SYS_STATE_STREAM_RECORD ||
                    target_state == SYS_STATE_LOW_POWER     ||
                    target_state == SYS_STATE_ERROR         ||
                    target_state == SYS_STATE_SHUTDOWN);

        case SYS_STATE_STREAM_RECORD:
            return (target_state == SYS_STATE_STREAMING     ||
                    target_state == SYS_STATE_RECORDING     ||
                    target_state == SYS_STATE_STANDBY       ||
                    target_state == SYS_STATE_LOW_POWER     ||
                    target_state == SYS_STATE_ERROR         ||
                    target_state == SYS_STATE_SHUTDOWN);

        case SYS_STATE_CALIBRATION:
            return (target_state == SYS_STATE_STANDBY ||
                    target_state == SYS_STATE_ERROR   ||
                    target_state == SYS_STATE_SHUTDOWN);

        case SYS_STATE_LOW_POWER:
            return (target_state == SYS_STATE_SHUTDOWN ||
                    target_state == SYS_STATE_STANDBY);

        case SYS_STATE_ERROR:
            /* Can recover to STANDBY or shut down */
            return (target_state == SYS_STATE_STANDBY ||
                    target_state == SYS_STATE_SHUTDOWN);

        case SYS_STATE_SHUTDOWN:
            /* Terminal state */
            return false;

        case SYS_STATE_UNKNOWN:
        default:
            return (target_state == SYS_STATE_INIT || target_state == SYS_STATE_ERROR);
    }
}

proto_status_t StateMachine::transitionTo(proto_sys_state_t target_state,
                                          uint16_t reason_code,
                                          bool force) {
    if (!force && !canTransitionTo(target_state)) {
        return PROTO_ERR_NOT_PERMITTED;
    }

    proto_sys_state_t old_state = current_state_;
    previous_state_ = old_state;
    current_state_  = target_state;
    state_enter_time_ = std::chrono::steady_clock::now();

    if (on_state_changed_) {
        on_state_changed_(old_state, target_state, reason_code);
    }

    return PROTO_OK;
}

void StateMachine::setStateChangeHandler(StateChangeHandler handler) {
    on_state_changed_ = std::move(handler);
}

uint32_t StateMachine::getStateUptimeSec() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state_enter_time_).count();
    return static_cast<uint32_t>(elapsed);
}

std::string_view StateMachine::getCurrentStateStr() const {
    return proto_sys_state_str(current_state_);
}

proto_sys_get_state_resp_t StateMachine::getStatusResponse() const {
    proto_sys_get_state_resp_t resp{};
    resp.status = (int32_t)PROTO_OK;
    resp.current_state = (uint8_t)current_state_;
    resp.previous_state = (uint8_t)previous_state_;
    resp.state_uptime_sec = getStateUptimeSec();
    return resp;
}

} // namespace uav::manager
