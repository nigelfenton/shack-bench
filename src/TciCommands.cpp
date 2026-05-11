#include "TciCommands.h"

#include <QMap>

namespace TciMon {

namespace {

// Display order for categories.  Anything not in this list ends up
// alphabetically after the listed ones.
constexpr const char* kCategoryOrder[] = {
    "Session / handshake",
    "Frequency / mode",
    "Transmit",
    "Audio / receiver state",
    "CW keyer",
    "Spots",
    "I/Q + audio streaming",
};

// All known commands.  Keep alphabetised within each category.  An
// empty `details` string means "no longer description on file" —
// the description dialog hides its "Show full details" button.
const std::vector<TciCommand>& tableData()
{
    static const std::vector<TciCommand> data = {
        // ── Session / handshake ──────────────────────────────────────
        { "protocol", "Session / handshake",
          "protocol:<name>,<version>;",
          "Server identifies itself and protocol version on connect.",
          "First message after a successful WebSocket handshake.  Used "
          "to identify the server software and protocol version so a "
          "client can adapt to vendor differences.  Example forms:\n"
          "  protocol:ExpertSDR2,1.6;\n"
          "  protocol:AetherSDR,0.9;\n"
          "Format varies between vendors — treat it as informational, "
          "not as a feature-detection mechanism."
        },
        { "device", "Session / handshake",
          "device:<name>;",
          "Hardware / SDR device name reported by the server.",
          ""
        },
        { "ready", "Session / handshake",
          "ready;",
          "Server has finished startup and will now respond to commands.",
          "Some servers emit this immediately after `protocol`.  Others "
          "wait until internal subsystems (codec, audio engine, hardware "
          "bring-up) are ready.  Clients should not send transmit-related "
          "commands until they have seen `ready`."
        },
        { "start", "Session / handshake",
          "start;",
          "Client → server. \"Begin sending live events to me.\"",
          "Without this, most servers stay silent after the handshake — "
          "they accept queries but emit no spontaneous events.  AetherSDR "
          "sends events as soon as the TCI panel is enabled regardless, "
          "but well-behaved clients send `start;` immediately after "
          "connecting for portability."
        },
        { "stop", "Session / handshake",
          "stop;",
          "Client → server. Stop streaming events.",
          "Counterpart to `start;`.  Useful when a client wants to stay "
          "connected but pause the firehose temporarily."
        },

        // ── Frequency / mode ─────────────────────────────────────────
        { "vfo", "Frequency / mode",
          "vfo:<rx>,<vfo>,<hz>;",
          "VFO frequency for receiver <rx>, knob <vfo> (0 = main, 1 = sub).",
          "Emitted whenever the operator tunes a receiver.  `<rx>` "
          "selects which receiver (0 is the primary RX).  `<vfo>` "
          "selects which knob — 0 is the main VFO, 1 is the sub VFO "
          "used by split or dual-watch.  `<hz>` is the absolute "
          "frequency in Hz (integer).\n"
          "\n"
          "Most loggers track only receiver 0 / VFO 0 — secondary "
          "receivers are used for diversity reception or working split "
          "but rarely belong in the contact record."
        },
        { "if", "Frequency / mode",
          "if:<rx>,<channel>,<hz>;",
          "IF offset from VFO centre, per receiver channel.",
          "Some servers report the carrier frequency via `if` rather "
          "than `vfo`, particularly for sub-channels within a receiver "
          "pan.  Treat `if:0,0,<hz>;` as a fallback for receivers that "
          "have not emitted a `vfo` event yet."
        },
        { "mode", "Frequency / mode",
          "mode:<rx>,<modeStr>;",
          "Emission mode for a receiver (USB / LSB / CW / AM / FM / DIG…).",
          "Common values: USB, LSB, CW, CWU, CWL, AM, SAM, FM, NFM, DFM, "
          "DIGU, DIGL, DIGI, RTTY, SPEC.\n"
          "\n"
          "For ADIF / Cabrillo logging, the digital sub-modes "
          "(DIGU / DIGL / DIGI) need an extra prompt to the operator — "
          "TCI alone doesn't tell you whether the sound-card program is "
          "running FT8, JS8, PSK, etc."
        },
        { "modulation", "Frequency / mode",
          "modulation:<rx>,<modeStr>;",
          "Synonym for `mode` emitted by some servers.",
          "AetherSDR uses `mode`.  ExpertSDR2 sometimes uses "
          "`modulation`.  Semantically identical — treat them as the "
          "same event."
        },
        { "rit_enable", "Frequency / mode",
          "rit_enable:<rx>,<true|false>;",
          "Receiver Incremental Tuning (RIT) on or off.",
          ""
        },
        { "rit_offset", "Frequency / mode",
          "rit_offset:<rx>,<hz>;",
          "RIT offset in Hz, signed.",
          ""
        },
        { "xit_enable", "Frequency / mode",
          "xit_enable:<rx>,<true|false>;",
          "Transmit Incremental Tuning (XIT) on or off.",
          ""
        },
        { "xit_offset", "Frequency / mode",
          "xit_offset:<rx>,<hz>;",
          "XIT offset in Hz, signed.",
          ""
        },

        // ── Transmit ─────────────────────────────────────────────────
        { "trx", "Transmit",
          "trx:<rx>,<true|false>;",
          "Switch the radio between receive and transmit.",
          "`<rx>` selects which receiver's PA / transmitter is keyed.  "
          "`true` = keyed, `false` = released.  Servers that support "
          "split or dual-RX may have more than one transmitter; most "
          "single-radio setups only ever see `trx:0,…`."
        },
        { "tune", "Transmit",
          "tune:<rx>,<true|false>;",
          "Engage low-power tune mode (sustained carrier).",
          "Distinct from `trx`.  `tune` emits a steady carrier at "
          "`tune_drive` watts for ATU adjustment.  `trx` is the normal "
          "SSB / CW transmit toggle."
        },
        { "drive", "Transmit",
          "drive:<percent>;",
          "TX power drive percentage (0–100).",
          "Most servers map this to the radio's normal power-output "
          "control; the absolute wattage depends on radio model, band, "
          "and PA configuration."
        },
        { "tune_drive", "Transmit",
          "tune_drive:<percent>;",
          "TX power drive used during tune mode.",
          ""
        },
        { "tx_power", "Transmit",
          "tx_power:<dBm>;",
          "Live forward-power reading while transmitting.",
          ""
        },
        { "tx_swr", "Transmit",
          "tx_swr:<ratio>;",
          "Live SWR reading while transmitting.",
          ""
        },

        // ── Audio / receiver state ───────────────────────────────────
        { "volume", "Audio / receiver state",
          "volume:<percent>;",
          "Main audio output gain (0–100).",
          ""
        },
        { "rx_volume", "Audio / receiver state",
          "rx_volume:<rx>,<chan>,<percent>;",
          "Per-channel audio gain.",
          ""
        },
        { "mute", "Audio / receiver state",
          "mute:<rx>,<true|false>;",
          "Mute or unmute a receiver.",
          ""
        },
        { "rx_enable", "Audio / receiver state",
          "rx_enable:<rx>,<true|false>;",
          "Enable or disable a receiver.",
          ""
        },
        { "rx_channel_enable", "Audio / receiver state",
          "rx_channel_enable:<rx>,<chan>,<true|false>;",
          "Enable or disable a sub-channel within a receiver.",
          ""
        },
        { "rx_filter_band", "Audio / receiver state",
          "rx_filter_band:<rx>,<lowHz>,<highHz>;",
          "DSP filter band-edge frequencies for a receiver.",
          "Negative numbers are valid for inverted modes (e.g. LSB).  "
          "Servers emit this whenever the operator drags a filter edge."
        },
        { "agc_mode", "Audio / receiver state",
          "agc_mode:<rx>,<name>;",
          "AGC speed setting.",
          "Common values: off, slow, medium, fast, custom."
        },
        { "sql_enable", "Audio / receiver state",
          "sql_enable:<rx>,<true|false>;",
          "Squelch on or off.",
          ""
        },
        { "sql_level", "Audio / receiver state",
          "sql_level:<rx>,<dB>;",
          "Squelch threshold in dB.",
          ""
        },
        { "rx_smeter", "Audio / receiver state",
          "rx_smeter:<rx>,<chan>,<dBm>;",
          "Live S-meter sample. HIGH-VOLUME — usually worth suppressing.",
          "Emitted at the server's metering rate, often 10–30 Hz.  "
          "This is by far the highest-volume event on the wire — it "
          "alone can dominate the log when running.  Suppress unless "
          "you specifically need to capture signal levels."
        },

        // ── CW keyer ─────────────────────────────────────────────────
        { "cw_keyer_speed", "CW keyer",
          "cw_keyer_speed:<wpm>;",
          "CW keyer speed in words per minute.",
          ""
        },
        { "cw_macros_speed", "CW keyer",
          "cw_macros_speed:<wpm>;",
          "Memory-keyer playback speed in words per minute.",
          "Some servers track this independently of the live keyer "
          "speed — sending macros at a different WPM from manual keying."
        },
        { "cw_msg", "CW keyer",
          "cw_msg:<rx>,<text>;",
          "Send memory-keyer macro text to be keyed.",
          ""
        },
        { "keyer", "CW keyer",
          "keyer:<rx>,<true|false>;",
          "Echo of an external CW key line (key down / up).",
          "Informational only — surfaced so loggers and panadapter "
          "overlays can show keying activity.  Authoritative keying "
          "happens on the hardware key line; TCI echoes over WiFi are "
          "far too slow to drive the radio directly."
        },

        // ── Spots ────────────────────────────────────────────────────
        { "spot", "Spots",
          "spot:<call>,<mode>,<hz>,<color>,<desc>;",
          "Add a spot to the panadapter overlay.",
          "The colour field can be hex (`0xFF0000`) or decimal "
          "(`16711680`) — both encode 24-bit RGB.  The description may "
          "itself contain commas; receivers should rejoin everything "
          "past the fourth field.  Spots persist on the server until "
          "either `spot_delete:<call>;` or `spot_clear;` is received."
        },
        { "spot_delete", "Spots",
          "spot_delete:<call>;",
          "Remove all spots matching a callsign.",
          "Emitted when a spot ages out, the operator works it, or the "
          "source feed retracts it."
        },
        { "spot_clear", "Spots",
          "spot_clear;",
          "Remove every spot currently shown.",
          "Typically emitted on disconnect from a DX-cluster source, "
          "or when the operator manually clears the panadapter."
        },

        // ── I/Q + audio streaming ────────────────────────────────────
        { "iq_samplerate", "I/Q + audio streaming",
          "iq_samplerate:<hz>;",
          "I/Q stream sample rate.",
          ""
        },
        { "iq_start", "I/Q + audio streaming",
          "iq_start:<rx>;",
          "Begin streaming I/Q samples for a receiver.",
          ""
        },
        { "iq_stop", "I/Q + audio streaming",
          "iq_stop:<rx>;",
          "Stop streaming I/Q samples for a receiver.",
          ""
        },
        { "iq_send_mode", "I/Q + audio streaming",
          "iq_send_mode:<mode>;",
          "Vendor-specific: which I/Q stream format the server emits.",
          "Servers differ on the value set used here.  Inspect what "
          "your server actually emits rather than relying on a fixed "
          "list."
        },
        { "audio_samplerate", "I/Q + audio streaming",
          "audio_samplerate:<hz>;",
          "Demodulated-audio stream sample rate.",
          ""
        },
        { "audio_stream_samples", "I/Q + audio streaming",
          "audio_stream_samples:<n>;",
          "Samples per audio frame in the stream.",
          ""
        },
        { "audio_stream_channels", "I/Q + audio streaming",
          "audio_stream_channels:<n>;",
          "Audio channels (1 = mono, 2 = stereo).",
          ""
        },
    };
    return data;
}

} // namespace

const TciCommand* findTciCommand(const QString& name)
{
    const QString key = name.trimmed().toLower();
    for (const auto& c : tableData()) {
        if (c.name == key) return &c;
    }
    return nullptr;
}

const std::vector<TciCommand>& allTciCommands()
{
    return tableData();
}

QStringList tciCommandCategories()
{
    QStringList out;
    for (const char* c : kCategoryOrder) out << QString::fromLatin1(c);
    return out;
}

} // namespace TciMon
