#pragma once

// Bluetooth A2DP sink domain: profile state machine and streaming session.
//
// HomeNX projects/a2dp-sink uses AVDTP + SDP to accept music from a phone.
// This header owns the state machine (not the BT stack) so it can be tested
// without btdrv.  The transport layer must call apply*() from the BT thread;
// the render thread calls only const getters.
//
// References: Bluetooth A2DP specification, BT SIG Assigned Numbers.

#include <cstdint>
#include <string>

namespace desknx::a2dp {

// ---- Codec -----------------------------------------------------------------

enum class Codec : unsigned char {
    Unknown,
    Sbc,   // mandatory A2DP codec
    Aac,
    Ldac,
};

// ---- Connection state machine ---------------------------------------------

enum class SinkState : unsigned char {
    Idle,           // BT radio off or not initialised
    Advertising,    // discoverable / waiting for a source
    Connecting,     // SDP + AVDTP signalling in progress
    Connected,      // AVDTP stream open, not streaming
    Streaming,      // audio data arriving
    Paused,         // stream suspended by the source
    Disconnecting,
    Failed,         // recoverable; can call startAdvertising() again
};

struct StreamSession {
    Codec       codec{Codec::Unknown};
    std::string sourceAddress;   // "XX:XX:XX:XX:XX:XX"
    std::string sourceName;      // Bluetooth device name
    std::uint32_t sampleRate{0}; // Hz (e.g. 44100, 48000)
    std::uint8_t  channels{0};   // 1 or 2
    std::uint32_t bitrate{0};    // bits/s, 0 = not known
};

class A2dpSink final {
public:
    SinkState state() const { return m_state; }

    bool isAvailable() const {
        return m_state == SinkState::Connected || m_state == SinkState::Streaming;
    }
    bool isStreaming() const { return m_state == SinkState::Streaming; }
    bool isIdle()      const { return m_state == SinkState::Idle; }

    const StreamSession& session() const { return m_session; }
    const std::string&   errorMessage() const { return m_errorMessage; }

    // ---- Transitions (called from the BT thread) -------------------------

    void startAdvertising() {
        m_state = SinkState::Advertising;
        m_session = StreamSession{};
        m_errorMessage.clear();
    }

    void setConnecting(const std::string& address, const std::string& name) {
        m_state = SinkState::Connecting;
        m_session.sourceAddress = address;
        m_session.sourceName    = name;
    }

    void setConnected(Codec codec, std::uint32_t sampleRate, std::uint8_t channels,
                      std::uint32_t bitrate = 0) {
        m_state               = SinkState::Connected;
        m_session.codec       = codec;
        m_session.sampleRate  = sampleRate;
        m_session.channels    = channels;
        m_session.bitrate     = bitrate;
        m_errorMessage.clear();
    }

    void startStream() {
        if (m_state == SinkState::Connected || m_state == SinkState::Paused)
            m_state = SinkState::Streaming;
    }

    void suspendStream() {
        if (m_state == SinkState::Streaming) m_state = SinkState::Paused;
    }

    void disconnect() {
        m_state   = SinkState::Disconnecting;
        m_session = StreamSession{};
    }

    void setIdle() {
        m_state   = SinkState::Idle;
        m_session = StreamSession{};
        m_errorMessage.clear();
    }

    void setFailed(const std::string& reason) {
        m_state        = SinkState::Failed;
        m_errorMessage = reason;
    }

    // After disconnect completes, go back to advertising.
    void finishDisconnect() {
        if (m_state == SinkState::Disconnecting) startAdvertising();
    }

    // ---- Codec display helpers -------------------------------------------

    static const char* codecName(Codec c) {
        switch (c) {
        case Codec::Sbc:  return "SBC";
        case Codec::Aac:  return "AAC";
        case Codec::Ldac: return "LDAC";
        default:          return "Unknown";
        }
    }

private:
    SinkState     m_state{SinkState::Idle};
    StreamSession m_session{};
    std::string   m_errorMessage;
};

} // namespace desknx::a2dp
