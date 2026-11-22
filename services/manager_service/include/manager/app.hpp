#ifndef __UAV_MANAGER_APP_HPP
#define __UAV_MANAGER_APP_HPP

#include <string>
#include <memory>
#include <atomic>

#include "manager/types.hpp"
#include "manager/broker.hpp"
#include "manager/state_machine.hpp"
#include "manager/supervisor.hpp"
#include "manager/system_monitor.hpp"

namespace uav::manager {

/**
 * @brief Master application engine for the Manager Service
 *
 * Implements a non-blocking Reactor pattern multiplexing OS signals, periodic timers,
 * and EIPC socket I/O on a single epoll event loop.
 */
class App {
public:
    explicit App(ManagerConfig config = {});
    ~App();

    /* Non-copyable, non-movable master service */
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    /**
     * @brief Initialize all subsystem descriptors (epoll, signalfd, timerfd, broker)
     */
    bool init();

    /**
     * @brief Start and run the blocking event loop until stop() is called
     * @return Process exit code (0 for clean exit, non-zero for error)
     */
    int run();

    /**
     * @brief Trigger graceful shutdown of the event loop
     */
    void stop();

    /* Accessors for subsystems */
    StateMachine& getStateMachine() noexcept { return fsm_; }
    Supervisor& getSupervisor() noexcept { return supervisor_; }
    SystemMonitor& getSystemMonitor() noexcept { return monitor_; }
    BrokerManager& getBroker() noexcept { return broker_; }

private:
    ManagerConfig config_;

    int epoll_fd_  = -1;
    int signal_fd_ = -1;
    int timer_fd_  = -1;

    std::atomic<bool> is_running_{false};

    BrokerManager broker_;
    StateMachine  fsm_;
    Supervisor    supervisor_;
    SystemMonitor monitor_;

    /* Epoll initialization helpers */
    bool setupSignalfd();
    bool setupTimerfd();
    bool setupEpoll();

    /* Event handlers */
    void handleSignalEvent();
    void handleTimerTick();
    void handleIncomingPacket(int peer_id, const proto_packet_t& packet);
    void handlePeerDisconnected(int peer_id);

    /* Protocol command dispatchers */
    void dispatchRegister(int peer_id, const proto_packet_t& pkt);
    void dispatchGetState(int peer_id, const proto_packet_t& pkt);
    void dispatchSetState(int peer_id, const proto_packet_t& pkt);
    void dispatchGetMetrics(int peer_id, const proto_packet_t& pkt);
    void dispatchDirectLinkReq(int peer_id, const proto_packet_t& pkt);
    void dispatchBatteryEvent(const proto_packet_t& pkt);

    /* State change orchestrator */
    void onFsmStateChanged(proto_sys_state_t old_state, proto_sys_state_t new_state, uint16_t reason);
};

} // namespace uav::manager

#endif // __UAV_MANAGER_APP_HPP
