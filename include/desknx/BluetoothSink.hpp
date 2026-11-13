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

{"content":"#pragma once\n\n// Bluetooth A2DP sink domain: profile state machine and streaming session.\n//\n// HomeNX projects/a2dp-sink uses AVDTP + SDP to accept music from a phone.\n// This header owns the state machine (not the BT stack) so it can be tested\n// without btdrv.  The transport layer must call apply*() from the BT thread;\n// the render thread calls only const getters.\n//\n// References: Bluetooth A2DP specification, BT SIG Assigned Numbers.\n\n#include <cstdint>\n#include <string>\n\nnamespace desknx::a2dp {\n\n// ---- Codec -----------------------------------------------------------------\n\nenum class Codec : unsigned char {\n    Unknown,\n    Sbc,   // mandatory A2DP codec\n    Aac,\n    Ldac,\n};\n\n// ---- Connection state machine ---------------------------------------------\n\nenum class SinkState : unsigned char {\n    Idle,           // BT radio off or not initialised\n    Advertising,    // discoverable / waiting for a source\n    Connecting,     // SDP + AVDTP signalling in progress\n    Connected,      // AVDTP stream open, not streaming\n    Streaming,      // audio data arriving\n    Paused,         // stream suspended by the source\n    Disconnecting,\n    Failed,         // recoverable; can call startAdvertising() again\n};\n\nstruct StreamSession {\n    Codec       codec{Codec::Unknown};\n    std::string sourceAddress;   // \"XX:XX:XX:XX:XX:XX\"\n    std::string sourceName;      // Bluetooth device name\n    std::uint32_t sampleRate{0}; // Hz (e.g. 44100, 48000)\n    std::uint8_t  channels{0};   // 1 or 2\n    std::uint32_t bitrate{0};    // bits/s, 0 = not known\n};\n\nclass A2dpSink final {\npublic:\n    SinkState state() const { return m_state; }\n\n    bool isAvailable() const {\n        return m_state == SinkState::Connected || m_state == SinkState::Streaming;\n    }\n    bool isStreaming() const { return m_state == SinkState::Streaming; }\n    bool isIdle()      const { return m_state == SinkState::Idle; }\n\n    const StreamSession& session() const { return m_session; }\n    const std::string&   errorMessage() const { return m_errorMessage; }\n\n    // ---- Transitions (called from the BT thread) -------------------------\n\n    void startAdvertising() {\n        m_state = SinkState::Advertising;\n        m_session = StreamSession{};\n        m_errorMessage.clear();\n    }\n\n    void setConnecting(const std::string& address, const std::string& name) {\n        m_state = SinkState::Connecting;\n        m_session.sourceAddress = address;\n        m_session.sourceName    = name;\n    }\n\n    void setConnected(Codec codec, std::uint32_t sampleRate, std::uint8_t channels,\n                      std::uint32_t bitrate = 0) {\n        m_state               = SinkState::Connected;\n        m_session.codec       = codec;\n        m_session.sampleRate  = sampleRate;\n        m_session.channels    = channels;\n        m_session.bitrate     = bitrate;\n        m_errorMessage.clear();\n    }\n\n    void startStream() {\n        if (m_state == SinkState::Connected || m_state == SinkState::Paused)\n            m_state = SinkState::Streaming;\n    }\n\n    void suspendStream() {\n        if (m_state == SinkState::Streaming) m_state = SinkState::Paused;\n    }\n\n    void disconnect() {\n        m_state   = SinkState::Disconnecting;\n        m_session = StreamSession{};\n    }\n\n    void setIdle() {\n        m_state   = SinkState::Idle;\n        m_session = StreamSession{};\n        m_errorMessage.clear();\n    }\n\n    void setFailed(const std::string& reason) {\n        m_state        = SinkState::Failed;\n        m_errorMessage = reason;\n    }\n\n    // After disconnect completes, go back to advertising.\n    void finishDisconnect() {\n        if (m_state == SinkState::Disconnecting) startAdvertising();\n    }\n\n    // ---- Codec display helpers -------------------------------------------\n\n    static const char* codecName(Codec c) {\n        switch (c) {\n        case Codec::Sbc:  return \"SBC\";\n        case Codec::Aac:  return \"AAC\";\n        case Codec::Ldac: return \"LDAC\";\n        default:          return \"Unknown\";\n        }\n    }\n\nprivate:\n    SinkState     m_state{SinkState::Idle};\n    StreamSession m_session{};\n    std::string   m_errorMessage;\n};\n\n} // namespace desknx::a2dp\n"}