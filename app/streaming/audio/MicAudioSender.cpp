// MicAudioSender — client-side mic capture + Opus encode + send pipeline.
//
// Wire-format reference: docs/design/WIRE.md section 3 (in the umbrella repo).
//
// Mid-stream toggle of the mic preference is intentionally NOT honoured. The
// streaming session (C3) reads the toggle once at stream start and passes it to
// start(). Users must restart the stream to change the setting.

#include "MicAudioSender.h"

#include <Limelight.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

#ifdef DEBUG_MIC_AB_CAPTURE
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <SDL_filesystem.h>
#endif

namespace {

// --- Audio constants -------------------------------------------------------

constexpr int    kSampleRate   = 48000;
constexpr int    kChannels     = 1;
constexpr int    kFrameSamples = 960;   // 48000 Hz × 20 ms = 960 samples/frame

// kFrameBytes: the exact number of bytes for one 20 ms mono s16 PCM frame.
constexpr size_t kFrameBytes   = (size_t)kFrameSamples * kChannels * sizeof(opus_int16);

// kBufBytes: PCM frame buffer size.
//
// pcmBuf holds exactly one decoded 20 ms frame after memcpy from the scratch
// buffer. The +256 padding is defense-in-depth only — it is NOT the primary
// safety mechanism against SDL_DequeueAudio over-delivery. The scratch buffer
// (kScratchBytes, allocated in runWorker) is the structural fix: SDL writes
// into scratch[], and memcpy clamps the copy into pcmBuf to at most to_read
// bytes per iteration. See the dequeue loop comment in runWorker().
constexpr size_t kBufBytes     = kFrameBytes + 256;

// kScratchBytes: scratch buffer for SDL_DequeueAudio.
//
// SDL_DequeueAudio writes into this buffer — never directly into pcmBuf. Sized
// generously so no documented or plausible sdl2-compat over-delivery can
// overflow it. After each dequeue, exactly min(got, to_read) bytes are
// memcpy'd into pcmBuf.
constexpr size_t kScratchBytes = kFrameBytes * 2 + 4096;

// Generously sized Opus output buffer. For VOIP at 48 kbit/s the actual encoded
// speech frame is typically 60–150 bytes, but we must leave room for the
// maximum possible output of opus_encode().
constexpr int    kMaxOpusBytes = 1500;

// --- Encoder configuration notes -------------------------------------------
//
// DELIBERATE SAFETY CHOICES — do not change without careful testing:
//
//   OPUS_APPLICATION_VOIP  (not OPUS_APPLICATION_AUDIO)
//   48 kbit/s bitrate
//   NO OPUS_SET_DTX call
//
// The POC experienced STATUS_STACK_BUFFER_OVERRUN (0xC0000409) crashes at
// consistent fault offsets when OPUS_APPLICATION_AUDIO was used together with
// OPUS_SET_DTX(0) at 64 kbit/s. Root cause was never isolated. To allow the
// rewrite to ship with known-stable configuration, OPUS_APPLICATION_VOIP at
// 48 kbit/s is used and OPUS_SET_DTX is NOT called (it remains at the encoder's
// own default). Any quality improvement — especially re-enabling AUDIO mode or
// DTX — requires a standalone harness to confirm stability under sustained
// speech load before landing here.
//
// Reference: moonlight-mic.md open thread "POC SDL2 fix may have stack-overrun
// bug" and plan question "Encoder configuration sweet spot".
constexpr int kBitrate = 64000;

bool containsCaseInsensitive(const std::string& haystack, const char* needle)
{
    const std::string needleString(needle);
    return std::search(
        haystack.begin(), haystack.end(),
        needleString.begin(), needleString.end(),
        [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) ==
                   std::tolower(static_cast<unsigned char>(b));
        }) != haystack.end();
}

void logCaptureDevices()
{
#if SDL_VERSION_ATLEAST(2, 24, 0)
    char* defaultCaptureName = nullptr;
    SDL_AudioSpec defaultCaptureSpec;
    SDL_zero(defaultCaptureSpec);
    if (SDL_GetDefaultAudioInfo(&defaultCaptureName, &defaultCaptureSpec, /*iscapture=*/1) == 0) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: default capture device: %s "
                    "(freq=%d fmt=0x%04x ch=%u samples=%u)",
                    defaultCaptureName != nullptr ? defaultCaptureName : "<unknown>",
                    defaultCaptureSpec.freq,
                    (unsigned)defaultCaptureSpec.format,
                    (unsigned)defaultCaptureSpec.channels,
                    (unsigned)defaultCaptureSpec.samples);
    }
    else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_GetDefaultAudioInfo(capture) failed: %s",
                    SDL_GetError());
    }
    if (defaultCaptureName != nullptr) {
        SDL_free(defaultCaptureName);
    }
#endif

    int captureDeviceCount = SDL_GetNumAudioDevices(/*iscapture=*/1);
    if (captureDeviceCount < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_GetNumAudioDevices(capture) failed: %s",
                    SDL_GetError());
        return;
    }

    for (int i = 0; i < captureDeviceCount; i++) {
        const char* name = SDL_GetAudioDeviceName(i, /*iscapture=*/1);
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: capture device[%d]: %s",
                    i,
                    name != nullptr ? name : "<unknown>");
    }
}

#ifdef DEBUG_MIC_AB_CAPTURE
// --- Debug A/B capture ---------------------------------------------------
//
// Compiled in only when DEBUG_MIC_AB_CAPTURE is defined at build time.
// The feature taps the pre-encode PCM stream into a WAV file for offline
// A/B comparison against the host-side post-decode WAV (Apollo's matching
// debug feature). See docs/development/mic-ab-capture.md.

constexpr int    kDebugCaptureSeconds   = 10;
constexpr size_t kDebugCaptureFrameCount = (size_t)kDebugCaptureSeconds * (1000 / 20);  // 500 frames
constexpr size_t kDebugCaptureSamples    = kDebugCaptureFrameCount * (size_t)kFrameSamples;  // 480000

// Resolve the debug capture output directory.
//
// Resolution order (mirroring the host-side env var convention so the
// workflow doc can describe one knob):
//   1. MOONLIGHT_MIC_AB_CAPTURE_DIR env var, if set.
//   2. SDL_GetPrefPath("moonlight-mic", "ab-capture") — cross-platform user dir.
//
// Returns an empty string only if SDL_GetPrefPath fails, which is unusual.
std::string resolveDebugCaptureDir() {
    if (const char* env = std::getenv("MOONLIGHT_MIC_AB_CAPTURE_DIR")) {
        if (env[0] != '\0') {
            return std::string(env);
        }
    }
    char* prefPath = SDL_GetPrefPath("moonlight-mic", "ab-capture");
    if (prefPath == nullptr) {
        return std::string();
    }
    std::string out(prefPath);
    SDL_free(prefPath);
    return out;
}

// Build a timestamped WAV filename: client-pre-encode-YYYYMMDD-HHMMSS.wav.
std::string buildDebugCapturePath(const std::string& dir) {
    std::time_t now = std::time(nullptr);
    std::tm tmBuf;
#ifdef _WIN32
    localtime_s(&tmBuf, &now);
#else
    localtime_r(&now, &tmBuf);
#endif
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp),
                  "%04d%02d%02d-%02d%02d%02d",
                  tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
                  tmBuf.tm_hour, tmBuf.tm_min, tmBuf.tm_sec);
    std::string path = dir;
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path.push_back('/');
    }
    path += "client-pre-encode-";
    path += stamp;
    path += ".wav";
    return path;
}

// Write a 44-byte WAV header for 48 kHz mono signed-16-bit PCM with the
// given total sample count. Format reference: http://soundfile.sapp.org/doc/WaveFormat/
// Numbers are written little-endian, which is the WAV file format convention
// and matches s16 PCM byte order on every platform we ship to.
void writeWavHeader(FILE* f, std::uint32_t sampleCount) {
    auto write_u32_le = [&](std::uint32_t v) {
        unsigned char b[4] = {
            static_cast<unsigned char>(v & 0xff),
            static_cast<unsigned char>((v >> 8) & 0xff),
            static_cast<unsigned char>((v >> 16) & 0xff),
            static_cast<unsigned char>((v >> 24) & 0xff),
        };
        std::fwrite(b, 1, 4, f);
    };
    auto write_u16_le = [&](std::uint16_t v) {
        unsigned char b[2] = {
            static_cast<unsigned char>(v & 0xff),
            static_cast<unsigned char>((v >> 8) & 0xff),
        };
        std::fwrite(b, 1, 2, f);
    };

    const std::uint32_t bytesPerSample = (std::uint32_t)kChannels * sizeof(opus_int16);  // 2
    const std::uint32_t dataChunkBytes = sampleCount * bytesPerSample;
    const std::uint32_t riffChunkBytes = 36 + dataChunkBytes;

    std::fwrite("RIFF", 1, 4, f);
    write_u32_le(riffChunkBytes);
    std::fwrite("WAVE", 1, 4, f);

    std::fwrite("fmt ", 1, 4, f);
    write_u32_le(16);                               // PCM fmt chunk size
    write_u16_le(1);                                // PCM format
    write_u16_le((std::uint16_t)kChannels);         // 1 channel
    write_u32_le((std::uint32_t)kSampleRate);       // 48000 Hz
    write_u32_le((std::uint32_t)kSampleRate * bytesPerSample);  // byte rate
    write_u16_le((std::uint16_t)bytesPerSample);    // block align
    write_u16_le(16);                               // bits per sample

    std::fwrite("data", 1, 4, f);
    write_u32_le(dataChunkBytes);
}
#endif  // DEBUG_MIC_AB_CAPTURE

}  // namespace

MicAudioSender::MicAudioSender()
    : m_CaptureDevice(0),
      m_Encoder(nullptr),
      m_Stopping(false)
{
}

MicAudioSender::~MicAudioSender()
{
    // Defensive: stop() is idempotent; the owner should have already called it,
    // but guard against leaks if destruction happens without an explicit stop().
    stop();
}

bool MicAudioSender::start(const std::string& captureDeviceName)
{
    SDL_assert(m_CaptureDevice == 0);
    SDL_assert(m_Encoder == nullptr);
    SDL_assert(!m_WorkerThread.joinable());

    // Init the SDL audio subsystem if it isn't already.
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_InitSubSystem(AUDIO) failed: %s",
                    SDL_GetError());
        return false;
    }

    logCaptureDevices();

    std::string effectiveCaptureDeviceName = captureDeviceName;
    if (const char* envCaptureDeviceName = SDL_getenv("MOONLIGHT_MIC_CAPTURE_DEVICE")) {
        if (envCaptureDeviceName[0] != '\0') {
            effectiveCaptureDeviceName = envCaptureDeviceName;
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: using MOONLIGHT_MIC_CAPTURE_DEVICE override: %s",
                        effectiveCaptureDeviceName.c_str());
        }
    }

    const char* captureDeviceNamePtr =
        effectiveCaptureDeviceName.empty() ? nullptr : effectiveCaptureDeviceName.c_str();

    if (!effectiveCaptureDeviceName.empty()) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: requested capture device: %s",
                    effectiveCaptureDeviceName.c_str());
        if (containsCaseInsensitive(effectiveCaptureDeviceName, "hands-free") ||
                containsCaseInsensitive(effectiveCaptureDeviceName, "headset")) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: selected capture device appears to be a Bluetooth "
                        "hands-free/headset endpoint; opening it can force local headset "
                        "playback into low-quality call mode");
        }
    }
    else {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: requested capture device: <system default>");
    }

    // Request: 48 kHz, mono, signed 16-bit, pull mode (callback=nullptr).
    // allowed_changes=0: reject devices that can't honour the exact format,
    // so we know the dequeued bytes are s16 without format conversion.
    SDL_AudioSpec want, have;
    SDL_zero(want);
    want.freq     = kSampleRate;
    want.format   = AUDIO_S16;
    want.channels = (Uint8)kChannels;
    want.samples  = (Uint16)kFrameSamples;
    want.callback = nullptr;    // pull mode

    m_CaptureDevice = SDL_OpenAudioDevice(
        captureDeviceNamePtr, /*iscapture=*/1, &want, &have, /*allowed_changes=*/0);
    if (m_CaptureDevice == 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: SDL_OpenAudioDevice(capture, %s) failed: %s",
                    captureDeviceNamePtr != nullptr ? captureDeviceNamePtr : "<system default>",
                    SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    // Create the Opus encoder.
    // See encoder configuration notes in the anonymous namespace above for
    // why OPUS_APPLICATION_VOIP is used and why OPUS_SET_DTX is not called.
    int err = OPUS_OK;
    m_Encoder = opus_encoder_create(kSampleRate, kChannels, OPUS_APPLICATION_VOIP, &err);
    if (m_Encoder == nullptr || err != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: opus_encoder_create failed: %d", err);
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    if (opus_encoder_ctl(m_Encoder, OPUS_SET_BITRATE(kBitrate)) != OPUS_OK) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: OPUS_SET_BITRATE(%d) failed", kBitrate);
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return false;
    }

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: capture device opened: %s "
                "(freq=%d fmt=0x%04x ch=%u samples=%u)",
                captureDeviceNamePtr != nullptr ? captureDeviceNamePtr : "<system default>",
                have.freq, (unsigned)have.format,
                (unsigned)have.channels, (unsigned)have.samples);

    m_Stopping.store(false, std::memory_order_relaxed);

    // Unpause so the device starts filling the SDL queue before the worker
    // first calls SDL_GetQueuedAudioSize().
    SDL_PauseAudioDevice(m_CaptureDevice, 0);

    m_WorkerThread = std::thread(&MicAudioSender::runWorker, this);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: started (48 kHz mono, VOIP, 20 ms, %d kbit/s)",
                kBitrate / 1000);
    return true;
}

void MicAudioSender::stop()
{
    // Idempotent: nothing to tear down if start() was never called or already
    // returned false.
    if (!m_WorkerThread.joinable() && m_CaptureDevice == 0 && m_Encoder == nullptr) {
        return;
    }

    m_Stopping.store(true, std::memory_order_relaxed);

    if (m_WorkerThread.joinable()) {
        m_WorkerThread.join();
    }

    if (m_CaptureDevice != 0) {
        SDL_PauseAudioDevice(m_CaptureDevice, 1);
        SDL_CloseAudioDevice(m_CaptureDevice);
        m_CaptureDevice = 0;
    }

    if (m_Encoder != nullptr) {
        opus_encoder_destroy(m_Encoder);
        m_Encoder = nullptr;
    }

#ifdef DEBUG_MIC_AB_CAPTURE
    // The worker thread has joined; m_DebugCaptureFile is no longer being
    // touched. If a partial capture was in progress, finalize the WAV with
    // whatever samples we got.
    if (m_DebugCaptureFile != nullptr) {
        std::fseek(m_DebugCaptureFile, 0, SEEK_SET);
        writeWavHeader(m_DebugCaptureFile, m_DebugCaptureSamplesWritten);
        std::fclose(m_DebugCaptureFile);
        m_DebugCaptureFile = nullptr;
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: debug capture finalised on stop (partial: %u samples)",
                    m_DebugCaptureSamplesWritten);
        m_DebugCaptureSamplesWritten = 0;
        m_DebugCaptureArmed.store(false, std::memory_order_release);
    }
#endif

    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "MicAudioSender: stopped");
}

#ifdef DEBUG_MIC_AB_CAPTURE
bool MicAudioSender::armDebugCapture()
{
    // Already armed or capture in progress on the worker side: ignore.
    bool expected = false;
    if (!m_DebugCaptureArmed.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel)) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "MicAudioSender: debug capture already armed/in progress, ignoring trigger");
        return false;
    }
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                "MicAudioSender: debug capture ARMED (will capture next %d s of pre-encode mic samples)",
                kDebugCaptureSeconds);

    // Also signal the host to start its capture by creating the `.arm` sentinel
    // file in the configured capture directory. When MOONLIGHT_MIC_AB_CAPTURE_DIR
    // points at a shared filesystem location (e.g. <your-drive>\... on JimothySnicket's setup,
    // visible as <your-drive>\... on host-pc) and the host's APOLLO_MIC_AB_CAPTURE_DIR
    // points at the same physical path, the host's polling loop sees the file
    // within ~1 second and starts its own capture. Result: one trigger, two
    // captures starting near-simultaneously.
    //
    // If MOONLIGHT_MIC_AB_CAPTURE_DIR is not set or doesn't resolve to a shared
    // path, the .arm file lands locally on the client and the host won't see
    // it — falls back to the prior workflow (manual SSH .arm creation).
    std::string dir = resolveDebugCaptureDir();
    if (!dir.empty()) {
        std::string armPath = dir;
        if (armPath.back() != '/' && armPath.back() != '\\') {
            armPath.push_back('/');
        }
        armPath += ".arm";
        FILE* f = std::fopen(armPath.c_str(), "wb");
        if (f != nullptr) {
            std::fclose(f);
            SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: wrote host trigger sentinel -> %s", armPath.c_str());
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: failed to write host trigger sentinel at %s "
                        "(host won't auto-arm; create the .arm file manually if needed)",
                        armPath.c_str());
        }
    }
    return true;
}
#endif

void MicAudioSender::runWorker()
{
    // HEAP-allocated PCM buffer and scratch buffer.
    //
    // pcmBuf receives exactly one 20 ms frame of s16 PCM per encode iteration,
    // written via memcpy from scratchBuf — never via SDL_DequeueAudio directly.
    // Invariant: pcmBuf never receives more than to_read bytes per iteration.
    //
    // scratchBuf is the SDL_DequeueAudio target. It is large enough that no
    // documented or plausible sdl2-compat over-delivery can overflow it. This is
    // the structural fix for the heap-buffer-overflow bug where sdl2-compat
    // delivers more bytes than requested, corrupting heap-adjacent objects.
    std::unique_ptr<uint8_t[]> pcmBuf(new uint8_t[kBufBytes]);
    std::unique_ptr<uint8_t[]> scratchBuf(new uint8_t[kScratchBytes]);
    unsigned char opusBuf[kMaxOpusBytes];
    uint16_t seqNumber = 0;

    while (!m_Stopping.load(std::memory_order_relaxed)) {
        // --- Accumulate a full 20 ms frame across as many SDL_DequeueAudio
        //     calls as needed.
        //
        // sdl2-compat (SDL2 shim over SDL3) returns audio in 960-byte chunks
        // regardless of how much was requested, even when the queue reports
        // ≥ 1920 bytes available. A single dequeue is therefore not sufficient
        // to fill a 1920-byte frame. We loop until full.
        //
        // CRITICAL: the `got` return value is clamped to `to_read` before
        // accumulation. sdl2-compat has been observed delivering MORE bytes
        // than requested; without this clamp the write pointer advances past
        // the buffer boundary, causing heap corruption (or stack overrun when
        // the buffer is stack-allocated).
        size_t bytes_filled = 0;
        while (bytes_filled < kFrameBytes &&
               !m_Stopping.load(std::memory_order_relaxed)) {
            if (SDL_GetQueuedAudioSize(m_CaptureDevice) == 0) {
                SDL_Delay(2);
                continue;
            }
            Uint32 to_read = static_cast<Uint32>(kFrameBytes - bytes_filled);
            // SDL_DequeueAudio writes into scratchBuf[], never into pcmBuf directly.
            // Invariant: pcmBuf never receives more than to_read bytes per iteration.
            Uint32 got = SDL_DequeueAudio(
                m_CaptureDevice,
                scratchBuf.get(),
                to_read);
            if (got > to_read) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "MicAudioSender: SDL_DequeueAudio over-delivery: "
                            "got=%u to_read=%u (clamped)",
                            (unsigned)got, (unsigned)to_read);
            }
            Uint32 copy_size = std::min(got, to_read);
            std::memcpy(pcmBuf.get() + bytes_filled, scratchBuf.get(), copy_size);
            bytes_filled += copy_size;
        }

        if (m_Stopping.load(std::memory_order_relaxed)) {
            break;
        }
        if (bytes_filled != kFrameBytes) {
            // Partial frame on a non-stopping exit — shouldn't happen, skip.
            continue;
        }

#ifdef DEBUG_MIC_AB_CAPTURE
        // Pre-encode capture tap. Pure write of raw s16 PCM samples — no
        // re-routing of the encoder pipeline. Even when DEBUG_MIC_AB_CAPTURE
        // is compiled in, the hot path adds at most an atomic load + a
        // conditional branch when no capture is armed. When the flag is not
        // defined at build time, this entire block is removed by the
        // preprocessor.
        if (m_DebugCaptureArmed.load(std::memory_order_acquire)) {
            // First-time entry into a capture window: open the WAV file.
            if (m_DebugCaptureFile == nullptr) {
                std::string dir = resolveDebugCaptureDir();
                if (dir.empty()) {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "MicAudioSender: debug capture: SDL_GetPrefPath failed; "
                                "set MOONLIGHT_MIC_AB_CAPTURE_DIR to override");
                    m_DebugCaptureArmed.store(false, std::memory_order_release);
                } else {
                    std::string path = buildDebugCapturePath(dir);
                    m_DebugCaptureFile = std::fopen(path.c_str(), "wb");
                    if (m_DebugCaptureFile == nullptr) {
                        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                    "MicAudioSender: debug capture: fopen('%s') failed",
                                    path.c_str());
                        m_DebugCaptureArmed.store(false, std::memory_order_release);
                    } else {
                        // Provisional header; rewritten with final sample count
                        // when we close.
                        writeWavHeader(m_DebugCaptureFile, 0);
                        m_DebugCaptureSamplesWritten = 0;
                        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                    "MicAudioSender: debug capture STARTED -> %s",
                                    path.c_str());
                    }
                }
            }
            if (m_DebugCaptureFile != nullptr) {
                std::fwrite(pcmBuf.get(), 1, kFrameBytes, m_DebugCaptureFile);
                m_DebugCaptureSamplesWritten += kFrameSamples;
                if (m_DebugCaptureSamplesWritten >= kDebugCaptureSamples) {
                    // Capture window complete. Rewrite the header with the
                    // actual sample count and close.
                    std::fseek(m_DebugCaptureFile, 0, SEEK_SET);
                    writeWavHeader(m_DebugCaptureFile, m_DebugCaptureSamplesWritten);
                    std::fclose(m_DebugCaptureFile);
                    m_DebugCaptureFile = nullptr;
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "MicAudioSender: debug capture COMPLETE (%u samples = %d s)",
                                m_DebugCaptureSamplesWritten, kDebugCaptureSeconds);
                    m_DebugCaptureSamplesWritten = 0;
                    m_DebugCaptureArmed.store(false, std::memory_order_release);
                }
            }
        }
#endif

        // Encode the PCM frame.
        opus_int32 encodedLen = opus_encode(
            m_Encoder,
            reinterpret_cast<const opus_int16*>(pcmBuf.get()),
            kFrameSamples,
            opusBuf,
            static_cast<opus_int32>(sizeof(opusBuf)));

        if (encodedLen < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: opus_encode failed: %d", (int)encodedLen);
            continue;
        }

        // Send the encoded frame. LiSendMicAudioFrame() returns 0 on success
        // and a negative value on error (e.g. no active session, host doesn't
        // support mic). Log the first failure at warn level; subsequent frames
        // will keep looping until stop() is called.
        int sendResult = LiSendMicAudioFrame(
            opusBuf,
            static_cast<int>(encodedLen),
            seqNumber);
        if (sendResult < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "MicAudioSender: LiSendMicAudioFrame returned %d (seq %u)",
                        sendResult, (unsigned)seqNumber);
        }

        ++seqNumber;
    }
}
