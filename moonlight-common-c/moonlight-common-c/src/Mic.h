#pragma once

#include <stdint.h>

// Packet type for client-to-host mic audio frames, sent via the existing
// AES-GCM encrypted control stream on CTRL_CHANNEL_GENERIC.
// Allocated from the Sunshine 0x55xx extension range; 0x5504-0x550F left
// as gap for any Sunshine-side additions before this value.
#define SS_MIC_OPUS_PTYPE 0x5510

// Host-side feature flag: host advertises mic-input support in its SDP
// x-ss-general.featureFlags attribute (parsed into SunshineFeatureFlags).
// Bit value 0x0100 confirmed free against existing LI_FF_* allocations
// (0x01 = LI_FF_PEN_TOUCH_EVENTS, 0x02 = LI_FF_CONTROLLER_TOUCH_EVENTS).
// Matches WIRE.md placeholder.
// H3 (Apollo, host side) sets this bit; P3 (client side) reads it.
#define SS_FF_MIC_INPUT 0x0100

// Client-side feature flag: client advertises mic-input capability in its
// SDP x-ml-general.featureFlags attribute sent to the host during connection.
// Bit value 0x04 confirmed free against existing ML_FF_* allocations
// (0x01 = ML_FF_FEC_STATUS, 0x02 = ML_FF_SESSION_ID_V1).
// Matches WIRE.md placeholder.
// P3 (client side) emits this bit; H3 (Apollo, host side) reads it.
#define ML_FF_MIC_INPUT 0x04

#pragma pack(push, 1)

// Fields are big-endian (matches RTP audio convention used host-to-client).
// Immediately follows the inner NVCTL_ENET_PACKET_HEADER_V2 payload and
// precedes the Opus frame bytes.
typedef struct _SS_MIC_FRAME_HEADER {
    uint16_t sequenceNumber;   // BE16; monotonic, wraps at 65535, first packet = 0
    uint16_t opusFrameLength;  // BE16; byte length of the Opus payload following this struct
    uint32_t timestampSamples; // BE32; 48 kHz sample count since first frame this session
                               // (increments by 960 = 48000 * 0.020 per packet)
} SS_MIC_FRAME_HEADER, *PSS_MIC_FRAME_HEADER;

#pragma pack(pop)

// Cross-compiler static assert: _Static_assert is C11/GCC/Clang but not
// available in MSVC C mode. The typedef trick works in C89 through C23 and
// on MSVC without any language-standard flag.
typedef char _ss_mic_header_size_check[(sizeof(SS_MIC_FRAME_HEADER) == 8) ? 1 : -1];
