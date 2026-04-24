#include <gtest/gtest.h>
#include "drone/state_machine.h"
#include <thread>
#include <chrono>

TEST(StateMachineTest, InitialStateIsOffline)
{
    StateMachine sm;
    EXPECT_EQ(sm.GetState(), DroneConnectionState::Offline);
}

TEST(StateMachineTest, TelemetryTransitionsToOnline)
{
    StateMachine sm;
    sm.OnTelemetryReceived();
    EXPECT_EQ(sm.GetState(), DroneConnectionState::Online);
}

TEST(StateMachineTest, TimeoutTransitionsToLost)
{
    StateMachine sm;
    sm.OnTelemetryReceived();  // Offline → Online
    EXPECT_EQ(sm.GetState(), DroneConnectionState::Online);

    // 0 秒超时 → 立刻变成 Lost
    bool changed = sm.CheckTimeout(0);
    EXPECT_TRUE(changed);
    EXPECT_EQ(sm.GetState(), DroneConnectionState::Lost);
}

TEST(StateMachineTest, TelemetryAfterTimeoutResetsToOnline)
{
    StateMachine sm;
    sm.OnTelemetryReceived();  // Online

    sm.CheckTimeout(0);  // Online → Lost

    sm.OnTelemetryReceived();  // Lost → Online (Reconnect)
    EXPECT_EQ(sm.GetState(), DroneConnectionState::Online);
}

TEST(StateMachineTest, ResetGoesToOffline)
{
    StateMachine sm;
    sm.OnTelemetryReceived();  // Online
    sm.Reset();
    EXPECT_EQ(sm.GetState(), DroneConnectionState::Offline);
}

TEST(StateMachineTest, PowerOnEvent)
{
    StateEvent received_event;
    StateMachine sm([&](StateEvent e) { received_event = e; });

    sm.OnTelemetryReceived();  // Offline → Online → PowerOn
    EXPECT_EQ(received_event, StateEvent::PowerOn);
}

TEST(StateMachineTest, LostConnectionEvent)
{
    StateEvent received_event;
    StateMachine sm([&](StateEvent e) { received_event = e; });

    sm.OnTelemetryReceived();  // Online
    sm.CheckTimeout(0);        // Lost
    EXPECT_EQ(received_event, StateEvent::LostConnection);
}

TEST(StateMachineTest, ReconnectEvent)
{
    StateEvent received_event;
    StateMachine sm([&](StateEvent e) { received_event = e; });

    sm.OnTelemetryReceived();  // PowerOn
    sm.CheckTimeout(0);        // Lost
    sm.OnTelemetryReceived();  // Reconnect
    EXPECT_EQ(received_event, StateEvent::Reconnect);
}

TEST(StateMachineTest, NoTimeoutWhenNotOnline)
{
    StateMachine sm;
    // Offline 状态不超时
    bool changed = sm.CheckTimeout(0);
    EXPECT_FALSE(changed);
    EXPECT_EQ(sm.GetState(), DroneConnectionState::Offline);
}
