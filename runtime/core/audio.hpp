#pragma once
#include "../cfire_prelude.hpp"
#include "fs.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <AudioToolbox/AudioQueue.h>
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
#include <objc/runtime.h>
#include <objc/message.h>
#endif
#elif defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
#include <emscripten.h>
#elif defined(__linux__)
#include <dlfcn.h>
#include <pthread.h>
#include <time.h>
#endif

namespace cfire::audio {

enum class Waveform : std::int32_t {
    Sine = 0,
    Square = 1,
    Saw = 2,
    Triangle = 3,
    Noise = 4
};

enum class SfxPreset : std::int32_t {
    Tap = 0,
    Coin = 1,
    Laser = 2,
    Explosion = 3,
    PowerUp = 4,
    Jump = 5,
    Alert = 6
};

struct Wave {
    std::vector<float> samples{};
    std::int32_t sample_rate = 48000;
    std::int32_t channels = 2;

    [[nodiscard]] auto frame_count() const noexcept -> std::int32_t {
        if (channels <= 0) return 0;
        return static_cast<std::int32_t>(samples.size() / static_cast<std::size_t>(channels));
    }

    [[nodiscard]] auto duration_sec() const noexcept -> float {
        if (sample_rate <= 0 || channels <= 0) return 0.0f;
        return static_cast<float>(frame_count()) / static_cast<float>(sample_rate);
    }

    void apply_adsr(float attack_sec, float decay_sec, float sustain_level, float release_sec) noexcept {
        std::int32_t frames = frame_count();
        if (frames <= 0 || sample_rate <= 0 || channels <= 0) return;
        float sr = static_cast<float>(sample_rate);
        std::int32_t att_f = std::max(0, static_cast<std::int32_t>(attack_sec * sr));
        std::int32_t dec_f = std::max(0, static_cast<std::int32_t>(decay_sec * sr));
        std::int32_t rel_f = std::max(0, static_cast<std::int32_t>(release_sec * sr));
        float sus = std::clamp(sustain_level, 0.0f, 1.0f);
        std::int32_t rel_start = std::max(0, frames - rel_f);

        for (std::int32_t f = 0; f < frames; ++f) {
            float env = sus;
            if (f < att_f && att_f > 0) {
                env = static_cast<float>(f) / static_cast<float>(att_f);
            } else if (f < att_f + dec_f && dec_f > 0) {
                float t = static_cast<float>(f - att_f) / static_cast<float>(dec_f);
                env = 1.0f + (sus - 1.0f) * t;
            }
            if (f >= rel_start && rel_f > 0) {
                float t = static_cast<float>(f - rel_start) / static_cast<float>(rel_f);
                env *= std::max(0.0f, 1.0f - t);
            }
            for (std::int32_t c = 0; c < channels; ++c) {
                samples[static_cast<std::size_t>(f * channels + c)] *= env;
            }
        }
    }
};

namespace detail {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

[[nodiscard]] inline auto sample_oscillator(Waveform kind, float phase, std::uint32_t& rng_state) noexcept -> float {
    float p = phase - std::floor(phase);
    switch (kind) {
        case Waveform::Sine:
            return std::sin(p * kTwoPi);
        case Waveform::Square:
            return p < 0.5f ? 1.0f : -1.0f;
        case Waveform::Saw:
            return 2.0f * p - 1.0f;
        case Waveform::Triangle:
            return 1.0f - 4.0f * std::abs(p - 0.5f);
        case Waveform::Noise: {
            rng_state = rng_state * 1664525u + 1013904223u;
            float norm = static_cast<float>(rng_state & 0x00FFFFFFu) / static_cast<float>(0x00800000u);
            return norm - 1.0f;
        }
    }
    return 0.0f;
}

struct SoundData {
    std::vector<float> samples{}; // Interleaved stereo or mono
    std::int32_t sample_rate = 48000;
    std::int32_t channels = 2;
    std::atomic<float> volume{1.0f};
    std::atomic<float> pitch{1.0f};
    std::atomic<float> pan{0.0f}; // -1.0 (left) .. 0.0 (center) .. +1.0 (right)
    std::atomic<bool> looping{false};
};

struct ActiveVoice {
    std::shared_ptr<SoundData> data{};
    double cursor_frame = 0.0;
    bool paused = false;
    bool active = false;
};

struct StreamState {
    std::mutex mtx{};
    std::vector<float> ring{};
    std::size_t read_pos = 0;
    std::size_t write_pos = 0;
    std::size_t count = 0; // Number of float samples currently buffered
    std::int32_t sample_rate = 48000;
    std::int32_t channels = 2;
    std::atomic<float> volume{1.0f};
    std::atomic<bool> playing{true};
    std::atomic<bool> closed{false};
    std::atomic<float> rms{0.0f};
    std::atomic<float> peak{0.0f};

    explicit StreamState(std::int32_t sr, std::int32_t ch, std::size_t cap_frames = 32768)
        : ring(cap_frames * static_cast<std::size_t>(std::max(1, ch)), 0.0f),
          sample_rate(std::max(8000, sr)),
          channels(std::clamp(ch, 1, 2)) {}

    void push_interleaved(std::span<const float> src) noexcept {
        if (src.empty() || closed.load(std::memory_order_relaxed)) return;
        float sum_sq = 0.0f;
        float pk = 0.0f;
        for (float v : src) {
            float av = std::abs(v);
            sum_sq += v * v;
            if (av > pk) pk = av;
        }
        float cur_rms = std::sqrt(sum_sq / static_cast<float>(src.size()));
        rms.store(std::clamp(cur_rms, 0.0f, 1.0f), std::memory_order_relaxed);
        peak.store(std::clamp(pk, 0.0f, 1.0f), std::memory_order_relaxed);

        std::lock_guard<std::mutex> lock(mtx);
        std::size_t cap = ring.size();
        if (cap == 0) return;
        for (float s : src) {
            ring[write_pos] = std::clamp(s, -1.0f, 1.0f);
            write_pos = (write_pos + 1) % cap;
            if (count < cap) {
                ++count;
            } else {
                read_pos = (read_pos + 1) % cap;
            }
        }
    }

    auto pop_stereo_add(float* out_stereo, std::int32_t frames, std::int32_t device_sr) noexcept -> std::int32_t {
        if (!playing.load(std::memory_order_relaxed) || closed.load(std::memory_order_relaxed)) return 0;
        std::lock_guard<std::mutex> lock(mtx);
        if (count == 0) {
            rms.store(rms.load(std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);
            peak.store(peak.load(std::memory_order_relaxed) * 0.85f, std::memory_order_relaxed);
            return 0;
        }
        float vol = volume.load(std::memory_order_relaxed);
        std::size_t cap = ring.size();
        std::size_t ch = static_cast<std::size_t>(channels);
        (void)device_sr;
        std::int32_t mixed = 0;
        for (std::int32_t f = 0; f < frames; ++f) {
            if (count < ch) break;
            float l = ring[read_pos];
            read_pos = (read_pos + 1) % cap;
            float r = (ch == 2) ? ring[read_pos] : l;
            if (ch == 2) read_pos = (read_pos + 1) % cap;
            count -= ch;
            out_stereo[f * 2] += l * vol;
            out_stereo[f * 2 + 1] += r * vol;
            ++mixed;
        }
        return mixed;
    }

    [[nodiscard]] auto queued_frames() noexcept -> std::int32_t {
        std::lock_guard<std::mutex> lock(mtx);
        return static_cast<std::int32_t>(count / static_cast<std::size_t>(std::max(1, channels)));
    }

    void clear() noexcept {
        std::lock_guard<std::mutex> lock(mtx);
        read_pos = 0;
        write_pos = 0;
        count = 0;
        rms.store(0.0f, std::memory_order_relaxed);
        peak.store(0.0f, std::memory_order_relaxed);
    }
};

#if defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
inline void activate_ios_audio_session() noexcept {
    // Dynamically configure AVAudioSessionCategoryPlayback via Objective-C runtime
    // so both .cpp and .mm targets on iOS play audio cleanly even with the silent switch on.
    Class session_cls = objc_getClass("AVAudioSession");
    if (!session_cls) return;
    SEL shared_sel = sel_registerName("sharedInstance");
    using SharedFn = id (*)(Class, SEL);
    id session = reinterpret_cast<SharedFn>(objc_msgSend)(session_cls, shared_sel);
    if (!session) return;

    Class nsstr_cls = objc_getClass("NSString");
    if (!nsstr_cls) return;
    SEL utf8_sel = sel_registerName("stringWithUTF8String:");
    using StrFn = id (*)(Class, SEL, const char*);
    id category = reinterpret_cast<StrFn>(objc_msgSend)(nsstr_cls, utf8_sel, "AVAudioSessionCategoryPlayback");

    SEL set_cat_sel = sel_registerName("setCategory:error:");
    if (class_respondsToSelector(object_getClass(session), set_cat_sel)) {
        using SetCatFn = bool (*)(id, SEL, id, id*);
        id err = nullptr;
        (void)reinterpret_cast<SetCatFn>(objc_msgSend)(session, set_cat_sel, category, &err);
    }
    SEL set_act_sel = sel_registerName("setActive:error:");
    if (class_respondsToSelector(object_getClass(session), set_act_sel)) {
        using SetActFn = bool (*)(id, SEL, bool, id*);
        id err = nullptr;
        (void)reinterpret_cast<SetActFn>(objc_msgSend)(session, set_act_sel, true, &err);
    }
}
#endif

struct Engine {
    std::mutex mtx{};
    std::atomic<bool> ready{false};
    std::atomic<bool> headless{false};
    std::int32_t sample_rate = 48000;
    std::int32_t channels = 2;
    std::atomic<float> master_vol{1.0f};
    std::atomic<float> master_rms{0.0f};
    std::atomic<float> master_peak{0.0f};
    std::vector<ActiveVoice> voices{};
    std::vector<std::shared_ptr<StreamState>> streams{};

#if defined(__APPLE__)
    AudioQueueRef queue = nullptr;
    static constexpr int kNumBuffers = 3;
    static constexpr int kBufferFrames = 1024;
    AudioQueueBufferRef buffers[kNumBuffers]{};
#elif defined(__linux__)
    void* asound_handle = nullptr;
    void* pcm_handle = nullptr;
    pthread_t audio_thread{};
    std::atomic<bool> thread_running{false};
#endif

    void mix_frames(float* out_stereo, std::int32_t frames) noexcept {
        std::memset(out_stereo, 0, static_cast<std::size_t>(frames * 2) * sizeof(float));
        float mvol = master_vol.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(mtx);
            // 1. Mix active polyphonic Sound voices
            for (auto& v : voices) {
                if (!v.active || v.paused || !v.data) continue;
                const auto& sd = *v.data;
                std::int32_t total_frames = (sd.channels > 0)
                    ? static_cast<std::int32_t>(sd.samples.size() / static_cast<std::size_t>(sd.channels))
                    : 0;
                if (total_frames <= 0) {
                    v.active = false;
                    continue;
                }
                float svol = sd.volume.load(std::memory_order_relaxed);
                float spitch = std::clamp(sd.pitch.load(std::memory_order_relaxed), 0.125f, 4.0f);
                float span = std::clamp(sd.pan.load(std::memory_order_relaxed), -1.0f, 1.0f);
                bool loop = sd.looping.load(std::memory_order_relaxed);

                // Constant-power stereo pan law
                float pan_angle = (span + 1.0f) * (kPi * 0.25f);
                float gain_l = std::cos(pan_angle) * svol;
                float gain_r = std::sin(pan_angle) * svol;
                double step = (static_cast<double>(sd.sample_rate) / static_cast<double>(std::max(1, sample_rate))) * static_cast<double>(spitch);

                for (std::int32_t f = 0; f < frames; ++f) {
                    std::int32_t idx0 = static_cast<std::int32_t>(v.cursor_frame);
                    if (idx0 >= total_frames) {
                        if (loop) {
                            v.cursor_frame = std::fmod(v.cursor_frame, static_cast<double>(total_frames));
                            idx0 = static_cast<std::int32_t>(v.cursor_frame);
                        } else {
                            v.active = false;
                            break;
                        }
                    }
                    std::int32_t idx1 = (idx0 + 1 < total_frames) ? (idx0 + 1) : (loop ? 0 : idx0);
                    float frac = static_cast<float>(v.cursor_frame - static_cast<double>(idx0));
                    float s_l = 0.0f;
                    float s_r = 0.0f;
                    if (sd.channels == 1) {
                        float s0 = sd.samples[static_cast<std::size_t>(idx0)];
                        float s1 = sd.samples[static_cast<std::size_t>(idx1)];
                        s_l = s0 + (s1 - s0) * frac;
                        s_r = s_l;
                    } else {
                        float l0 = sd.samples[static_cast<std::size_t>(idx0 * 2)];
                        float r0 = sd.samples[static_cast<std::size_t>(idx0 * 2 + 1)];
                        float l1 = sd.samples[static_cast<std::size_t>(idx1 * 2)];
                        float r1 = sd.samples[static_cast<std::size_t>(idx1 * 2 + 1)];
                        s_l = l0 + (l1 - l0) * frac;
                        s_r = r0 + (r1 - r0) * frac;
                    }
                    out_stereo[f * 2] += s_l * gain_l;
                    out_stereo[f * 2 + 1] += s_r * gain_r;
                    v.cursor_frame += step;
                }
            }

            // 2. Mix active real-time AudioStreams
            for (auto& st : streams) {
                if (st && !st->closed.load(std::memory_order_relaxed)) {
                    (void)st->pop_stereo_add(out_stereo, frames, sample_rate);
                }
            }
        }

        // 3. Apply master gain, soft-clip protection, and compute live RMS / Peak metering
        float sum_sq = 0.0f;
        float pk = 0.0f;
        std::int32_t total_samples = frames * 2;
        for (std::int32_t i = 0; i < total_samples; ++i) {
            float s = std::clamp(out_stereo[i] * mvol, -1.0f, 1.0f);
            out_stereo[i] = s;
            sum_sq += s * s;
            float as = std::abs(s);
            if (as > pk) pk = as;
        }
        if (total_samples > 0) {
            float rms = std::sqrt(sum_sq / static_cast<float>(total_samples));
            // Smooth decay if quiet so UI meters remain responsive
            float prev_rms = master_rms.load(std::memory_order_relaxed);
            float prev_pk = master_peak.load(std::memory_order_relaxed);
            master_rms.store(std::max(rms, prev_rms * 0.75f), std::memory_order_relaxed);
            master_peak.store(std::max(pk, prev_pk * 0.75f), std::memory_order_relaxed);
        }
    }

#if defined(__APPLE__)
    static void apple_queue_cb(void* user_data, AudioQueueRef q, AudioQueueBufferRef buf) noexcept {
        auto* eng = static_cast<Engine*>(user_data);
        if (!eng || !buf) return;
        std::int32_t frames = static_cast<std::int32_t>(buf->mAudioDataBytesCapacity / (2 * sizeof(float)));
        auto* out = static_cast<float*>(buf->mAudioData);
        if (eng->ready.load(std::memory_order_relaxed)) {
            eng->mix_frames(out, frames);
        } else {
            std::memset(out, 0, buf->mAudioDataBytesCapacity);
        }
        buf->mAudioDataByteSize = static_cast<UInt32>(frames * 2 * sizeof(float));
        AudioQueueEnqueueBuffer(q, buf, 0, nullptr);
    }
#elif defined(__linux__)
    using snd_pcm_open_fn = int (*)(void**, const char*, int, int);
    using snd_pcm_set_params_fn = int (*)(void*, int, int, unsigned int, unsigned int, int, unsigned int);
    using snd_pcm_writei_fn = long (*)(void*, const void*, unsigned long);
    using snd_pcm_recover_fn = int (*)(void*, int, int);
    using snd_pcm_close_fn = int (*)(void*);

    snd_pcm_writei_fn fn_writei = nullptr;
    snd_pcm_recover_fn fn_recover = nullptr;
    snd_pcm_close_fn fn_close = nullptr;

    static auto linux_audio_thread(void* arg) noexcept -> void* {
        auto* eng = static_cast<Engine*>(arg);
        float buf[1024 * 2];
        while (eng->thread_running.load(std::memory_order_relaxed)) {
            eng->mix_frames(buf, 1024);
            if (eng->pcm_handle && eng->fn_writei) {
                long written = eng->fn_writei(eng->pcm_handle, buf, 1024);
                if (written < 0 && eng->fn_recover) {
                    (void)eng->fn_recover(eng->pcm_handle, static_cast<int>(written), 1);
                }
            } else {
                struct timespec ts{0, 15 * 1000 * 1000};
                nanosleep(&ts, nullptr);
            }
        }
        return nullptr;
    }
#endif
};

inline auto engine() noexcept -> Engine& {
    static Engine eng{};
    return eng;
}

#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
inline void wasm_pump_audio() noexcept {
    auto& eng = engine();
    if (!eng.ready.load(std::memory_order_relaxed) || eng.headless.load(std::memory_order_relaxed)) return;
    float temp[1024 * 2];
    eng.mix_frames(temp, 1024);
    EM_ASM({
        if (typeof window !== 'undefined' && window.__cfire_audio_push) {
            window.__cfire_audio_push($0, $1, $2);
        }
    }, temp, 1024, eng.sample_rate);
}
#endif

} // namespace detail

// ============================================================================
// Device Lifecycle & Metering
// ============================================================================

[[nodiscard]] inline auto init(std::int32_t sample_rate = 48000, std::int32_t channels = 2)
    -> std::expected<void, cfire::Error>
{
    auto& eng = detail::engine();
    if (eng.ready.load(std::memory_order_relaxed)) {
        return {};
    }
    eng.sample_rate = std::clamp(sample_rate, 8000, 96000);
    eng.channels = std::clamp(channels, 1, 2);

    const char* hl_env = std::getenv("CFIRE_HEADLESS_TEST");
    bool force_headless = (hl_env != nullptr && std::string_view(hl_env) == "1");
    eng.headless.store(force_headless, std::memory_order_relaxed);

    if (!force_headless) {
#if defined(__APPLE__)
#if defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE
        detail::activate_ios_audio_session();
#endif
        AudioStreamBasicDescription fmt{};
        fmt.mSampleRate = static_cast<Float64>(eng.sample_rate);
        fmt.mFormatID = kAudioFormatLinearPCM;
        fmt.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        fmt.mBitsPerChannel = 32;
        fmt.mChannelsPerFrame = 2;
        fmt.mBytesPerFrame = 2 * sizeof(float);
        fmt.mFramesPerPacket = 1;
        fmt.mBytesPerPacket = fmt.mBytesPerFrame;

        OSStatus st = AudioQueueNewOutput(&fmt, detail::Engine::apple_queue_cb, &eng, nullptr, nullptr, 0, &eng.queue);
        if (st == noErr && eng.queue != nullptr) {
            eng.ready.store(true, std::memory_order_relaxed);
            UInt32 buf_bytes = detail::Engine::kBufferFrames * fmt.mBytesPerFrame;
            for (int i = 0; i < detail::Engine::kNumBuffers; ++i) {
                AudioQueueAllocateBuffer(eng.queue, buf_bytes, &eng.buffers[i]);
                if (eng.buffers[i]) {
                    std::memset(eng.buffers[i]->mAudioData, 0, buf_bytes);
                    eng.buffers[i]->mAudioDataByteSize = buf_bytes;
                    AudioQueueEnqueueBuffer(eng.queue, eng.buffers[i], 0, nullptr);
                }
            }
            AudioQueueStart(eng.queue, nullptr);
            return {};
        }
        // Fallback to software headless mixer if no audio output hardware exists (e.g. macOS CI runner)
        eng.headless.store(true, std::memory_order_relaxed);
#elif defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
        EM_ASM({
            if (typeof window !== 'undefined' && !window.__cfire_audio_ctx) {
                var AudioCtx = window.AudioContext || window.webkitAudioContext;
                if (AudioCtx) {
                    var ctx = new AudioCtx({ sampleRate: $0 });
                    window.__cfire_audio_ctx = ctx;
                    window.__cfire_next_time = 0;
                    var unlock = function() {
                        if (ctx.state === 'suspended') ctx.resume();
                    };
                    window.addEventListener('pointerdown', unlock, { passive: true });
                    window.addEventListener('touchstart', unlock, { passive: true });
                    window.addEventListener('keydown', unlock, { passive: true });
                    window.__cfire_audio_push = function(ptr, frames, sr) {
                        if (ctx.state === 'suspended') return;
                        var buf = ctx.createBuffer(2, frames, sr);
                        var l = buf.getChannelData(0);
                        var r = buf.getChannelData(1);
                        var base = ptr >> 2;
                        for (var i = 0; i < frames; ++i) {
                            l[i] = HEAPF32[base + i * 2];
                            r[i] = HEAPF32[base + i * 2 + 1];
                        }
                        var src = ctx.createBufferSource();
                        src.buffer = buf;
                        src.connect(ctx.destination);
                        var now = ctx.currentTime;
                        if (window.__cfire_next_time < now) {
                            window.__cfire_next_time = now + 0.015;
                        }
                        src.start(window.__cfire_next_time);
                        window.__cfire_next_time += frames / sr;
                    };
                }
            }
        }, eng.sample_rate);
#elif defined(__linux__)
        eng.asound_handle = dlopen("libasound.so.2", RTLD_LAZY);
        if (eng.asound_handle) {
            auto fn_open = reinterpret_cast<detail::Engine::snd_pcm_open_fn>(dlsym(eng.asound_handle, "snd_pcm_open"));
            auto fn_params = reinterpret_cast<detail::Engine::snd_pcm_set_params_fn>(dlsym(eng.asound_handle, "snd_pcm_set_params"));
            eng.fn_writei = reinterpret_cast<detail::Engine::snd_pcm_writei_fn>(dlsym(eng.asound_handle, "snd_pcm_writei"));
            eng.fn_recover = reinterpret_cast<detail::Engine::snd_pcm_recover_fn>(dlsym(eng.asound_handle, "snd_pcm_recover"));
            eng.fn_close = reinterpret_cast<detail::Engine::snd_pcm_close_fn>(dlsym(eng.asound_handle, "snd_pcm_close"));
            // SND_PCM_STREAM_PLAYBACK = 0, SND_PCM_FORMAT_FLOAT_LE = 14, SND_PCM_ACCESS_RW_INTERLEAVED = 3
            if (fn_open && fn_params && eng.fn_writei &&
                fn_open(&eng.pcm_handle, "default", 0, 0) == 0 &&
                fn_params(eng.pcm_handle, 14, 3, 2, static_cast<unsigned int>(eng.sample_rate), 1, 40000) == 0) {
                eng.ready.store(true, std::memory_order_relaxed);
                eng.thread_running.store(true, std::memory_order_relaxed);
                pthread_create(&eng.audio_thread, nullptr, detail::Engine::linux_audio_thread, &eng);
                return {};
            }
        }
        eng.headless.store(true, std::memory_order_relaxed);
#else
        eng.headless.store(true, std::memory_order_relaxed);
#endif
    }

    eng.ready.store(true, std::memory_order_relaxed);
    return {};
}

inline void close() noexcept {
    auto& eng = detail::engine();
    if (!eng.ready.exchange(false, std::memory_order_relaxed)) return;
#if defined(__APPLE__)
    if (eng.queue) {
        AudioQueueStop(eng.queue, true);
        AudioQueueDispose(eng.queue, true);
        eng.queue = nullptr;
    }
#elif defined(__linux__)
    if (eng.thread_running.exchange(false, std::memory_order_relaxed)) {
        pthread_join(eng.audio_thread, nullptr);
    }
    if (eng.pcm_handle && eng.fn_close) {
        eng.fn_close(eng.pcm_handle);
        eng.pcm_handle = nullptr;
    }
    if (eng.asound_handle) {
        dlclose(eng.asound_handle);
        eng.asound_handle = nullptr;
    }
#endif
    std::lock_guard<std::mutex> lock(eng.mtx);
    eng.voices.clear();
    eng.streams.clear();
    eng.master_rms.store(0.0f, std::memory_order_relaxed);
    eng.master_peak.store(0.0f, std::memory_order_relaxed);
}

[[nodiscard]] inline auto is_ready() noexcept -> bool {
    return detail::engine().ready.load(std::memory_order_relaxed);
}

inline void set_master_volume(float vol) noexcept {
    detail::engine().master_vol.store(std::clamp(vol, 0.0f, 1.0f), std::memory_order_relaxed);
}

[[nodiscard]] inline auto master_volume() noexcept -> float {
    return detail::engine().master_vol.load(std::memory_order_relaxed);
}

inline void step_mixer(std::int32_t frames = 256) noexcept {
    auto& eng = detail::engine();
    if (!eng.ready.load(std::memory_order_relaxed)) return;
    std::int32_t f = std::clamp(frames, 1, 4096);
    std::vector<float> scratch(static_cast<std::size_t>(f * 2));
    eng.mix_frames(scratch.data(), f);
}

[[nodiscard]] inline auto rms_level() noexcept -> float {
    auto& eng = detail::engine();
    if (eng.headless.load(std::memory_order_relaxed)) {
        step_mixer(128);
    }
    return eng.master_rms.load(std::memory_order_relaxed);
}

[[nodiscard]] inline auto peak_level() noexcept -> float {
    auto& eng = detail::engine();
    if (eng.headless.load(std::memory_order_relaxed)) {
        step_mixer(128);
    }
    return eng.master_peak.load(std::memory_order_relaxed);
}

// ============================================================================
// Procedural Waveform & SFX Synthesis
// ============================================================================

[[nodiscard]] inline auto gen_tone(
    Waveform kind,
    float freq_hz,
    float duration_sec,
    float volume = 0.5f,
    std::int32_t sample_rate = 48000
) -> Wave {
    Wave w;
    w.sample_rate = std::max(8000, sample_rate);
    w.channels = 2;
    std::int32_t frames = std::max(1, static_cast<std::int32_t>(duration_sec * static_cast<float>(w.sample_rate)));
    w.samples.resize(static_cast<std::size_t>(frames * 2));

    float phase = 0.0f;
    float phase_inc = std::max(1.0f, freq_hz) / static_cast<float>(w.sample_rate);
    float gain = std::clamp(volume, 0.0f, 1.0f);
    std::uint32_t rng = 0x12345678u;

    for (std::int32_t f = 0; f < frames; ++f) {
        float s = detail::sample_oscillator(kind, phase, rng) * gain;
        w.samples[static_cast<std::size_t>(f * 2)] = s;
        w.samples[static_cast<std::size_t>(f * 2 + 1)] = s;
        phase += phase_inc;
    }
    w.apply_adsr(0.005f, 0.02f, 0.85f, std::min(0.04f, duration_sec * 0.25f));
    return w;
}

[[nodiscard]] inline auto gen_chirp(
    float start_hz,
    float end_hz,
    float duration_sec,
    Waveform kind = Waveform::Sine,
    float volume = 0.5f,
    std::int32_t sample_rate = 48000
) -> Wave {
    Wave w;
    w.sample_rate = std::max(8000, sample_rate);
    w.channels = 2;
    std::int32_t frames = std::max(1, static_cast<std::int32_t>(duration_sec * static_cast<float>(w.sample_rate)));
    w.samples.resize(static_cast<std::size_t>(frames * 2));

    float phase = 0.0f;
    float gain = std::clamp(volume, 0.0f, 1.0f);
    std::uint32_t rng = 0x87654321u;

    for (std::int32_t f = 0; f < frames; ++f) {
        float t = static_cast<float>(f) / static_cast<float>( std::max(1, frames - 1) );
        float freq = start_hz + (end_hz - start_hz) * t;
        float s = detail::sample_oscillator(kind, phase, rng) * gain;
        w.samples[static_cast<std::size_t>(f * 2)] = s;
        w.samples[static_cast<std::size_t>(f * 2 + 1)] = s;
        phase += std::max(1.0f, freq) / static_cast<float>(w.sample_rate);
    }
    w.apply_adsr(0.003f, 0.02f, 0.8f, std::min(0.05f, duration_sec * 0.3f));
    return w;
}

[[nodiscard]] inline auto gen_sfx(SfxPreset preset, std::int32_t sample_rate = 48000) -> Wave {
    switch (preset) {
        case SfxPreset::Tap: {
            auto w = gen_chirp(420.0f, 140.0f, 0.035f, Waveform::Sine, 0.45f, sample_rate);
            w.apply_adsr(0.001f, 0.01f, 0.4f, 0.02f);
            return w;
        }
        case SfxPreset::Coin: {
            Wave w1 = gen_tone(Waveform::Square, 987.77f, 0.07f, 0.35f, sample_rate); // B5
            Wave w2 = gen_tone(Waveform::Square, 1318.51f, 0.22f, 0.35f, sample_rate); // E6
            w1.samples.insert(w1.samples.end(), w2.samples.begin(), w2.samples.end());
            w1.apply_adsr(0.002f, 0.03f, 0.7f, 0.12f);
            return w1;
        }
        case SfxPreset::Laser:
            return gen_chirp(1200.0f, 180.0f, 0.16f, Waveform::Saw, 0.4f, sample_rate);
        case SfxPreset::Explosion: {
            auto w = gen_tone(Waveform::Noise, 100.0f, 0.45f, 0.6f, sample_rate);
            w.apply_adsr(0.005f, 0.12f, 0.35f, 0.30f);
            return w;
        }
        case SfxPreset::PowerUp:
            return gen_chirp(330.0f, 1320.0f, 0.28f, Waveform::Triangle, 0.45f, sample_rate);
        case SfxPreset::Jump:
            return gen_chirp(220.0f, 660.0f, 0.15f, Waveform::Square, 0.35f, sample_rate);
        case SfxPreset::Alert:
            return gen_chirp(880.0f, 440.0f, 0.20f, Waveform::Triangle, 0.45f, sample_rate);
    }
    return gen_tone(Waveform::Sine, 440.0f, 0.1f, 0.4f, sample_rate);
}

// ============================================================================
// Pure C++23 RIFF/WAV Codec (8-bit, 16-bit, 24-bit PCM & 32-bit IEEE Float)
// ============================================================================

[[nodiscard]] inline auto encode_wav(const Wave& wave) -> std::vector<std::byte> {
    std::int32_t ch = std::clamp(wave.channels, 1, 2);
    std::int32_t sr = std::max(8000, wave.sample_rate);
    std::uint32_t num_samples = static_cast<std::uint32_t>(wave.samples.size());
    std::uint32_t data_bytes = num_samples * sizeof(std::int16_t);
    std::uint32_t riff_size = 36u + data_bytes;

    std::vector<std::byte> out(44u + data_bytes);
    auto* p = reinterpret_cast<std::uint8_t*>(out.data());

    auto write_u16 = [&](std::size_t off, std::uint16_t v) {
        p[off] = static_cast<std::uint8_t>(v & 0xFFu);
        p[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    };
    auto write_u32 = [&](std::size_t off, std::uint32_t v) {
        p[off] = static_cast<std::uint8_t>(v & 0xFFu);
        p[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
        p[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
        p[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    };

    std::memcpy(p + 0, "RIFF", 4);
    write_u32(4, riff_size);
    std::memcpy(p + 8, "WAVE", 4);
    std::memcpy(p + 12, "fmt ", 4);
    write_u32(16, 16u); // PCM fmt chunk size
    write_u16(20, 1u);  // AudioFormat = 1 (PCM)
    write_u16(22, static_cast<std::uint16_t>(ch));
    write_u32(24, static_cast<std::uint32_t>(sr));
    write_u32(28, static_cast<std::uint32_t>(sr * ch * 2)); // ByteRate
    write_u16(32, static_cast<std::uint16_t>(ch * 2));      // BlockAlign
    write_u16(34, 16u);                                     // BitsPerSample
    std::memcpy(p + 36, "data", 4);
    write_u32(40, data_bytes);

    std::size_t off = 44;
    for (float s : wave.samples) {
        float clamped = std::clamp(s, -1.0f, 1.0f);
        std::int16_t pcm = static_cast<std::int16_t>(clamped * 32767.0f);
        write_u16(off, static_cast<std::uint16_t>(pcm));
        off += 2;
    }
    return out;
}

[[nodiscard]] inline auto decode_wav(std::span<const std::byte> data)
    -> std::expected<Wave, cfire::Error>
{
    if (data.size() < 44) {
        return cfire::make_error("WAV decode failed: header shorter than 44 bytes");
    }
    const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
    auto read_u16 = [&](std::size_t off) -> std::uint16_t {
        return static_cast<std::uint16_t>(p[off] | (static_cast<std::uint16_t>(p[off + 1]) << 8));
    };
    auto read_u32 = [&](std::size_t off) -> std::uint32_t {
        return static_cast<std::uint32_t>(p[off]) |
               (static_cast<std::uint32_t>(p[off + 1]) << 8) |
               (static_cast<std::uint32_t>(p[off + 2]) << 16) |
               (static_cast<std::uint32_t>(p[off + 3]) << 24);
    };

    if (std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) {
        return cfire::make_error("WAV decode failed: missing RIFF/WAVE magic header");
    }

    std::uint16_t audio_fmt = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::uint16_t bits_per_sample = 0;
    std::size_t data_offset = 0;
    std::size_t data_size = 0;

    std::size_t pos = 12;
    while (pos + 8 <= data.size()) {
        std::uint32_t chunk_size = read_u32(pos + 4);
        std::size_t body_pos = pos + 8;
        if (body_pos > data.size()) break;
        std::size_t avail = data.size() - body_pos;
        std::size_t safe_size = std::min<std::size_t>(chunk_size, avail);

        if (std::memcmp(p + pos, "fmt ", 4) == 0 && safe_size >= 16) {
            audio_fmt = read_u16(body_pos);
            channels = read_u16(body_pos + 2);
            sample_rate = read_u32(body_pos + 4);
            bits_per_sample = read_u16(body_pos + 14);
        } else if (std::memcmp(p + pos, "data", 4) == 0) {
            data_offset = body_pos;
            data_size = safe_size;
            break;
        }
        pos = body_pos + chunk_size + (chunk_size & 1u);
    }

    if (channels == 0 || sample_rate == 0 || data_offset == 0) {
        return cfire::make_error("WAV decode failed: missing fmt or data chunk");
    }
    if (audio_fmt != 1 && audio_fmt != 3) {
        return cfire::make_error(std::format("WAV decode failed: unsupported format tag {}", audio_fmt));
    }

    Wave w;
    w.sample_rate = static_cast<std::int32_t>(sample_rate);
    w.channels = static_cast<std::int32_t>(channels);

    if (audio_fmt == 1 && bits_per_sample == 16) {
        std::size_t count = data_size / 2;
        w.samples.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            auto raw = static_cast<std::int16_t>(read_u16(data_offset + i * 2));
            w.samples[i] = static_cast<float>(raw) / 32768.0f;
        }
    } else if (audio_fmt == 1 && bits_per_sample == 8) {
        w.samples.resize(data_size);
        for (std::size_t i = 0; i < data_size; ++i) {
            w.samples[i] = (static_cast<float>(p[data_offset + i]) - 128.0f) / 128.0f;
        }
    } else if (audio_fmt == 1 && bits_per_sample == 24) {
        std::size_t count = data_size / 3;
        w.samples.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            std::size_t b = data_offset + i * 3;
            std::int32_t v = static_cast<std::int32_t>(p[b]) |
                             (static_cast<std::int32_t>(p[b + 1]) << 8) |
                             (static_cast<std::int32_t>(static_cast<std::int8_t>(p[b + 2])) << 16);
            w.samples[i] = static_cast<float>(v) / 8388608.0f;
        }
    } else if (audio_fmt == 3 && bits_per_sample == 32) {
        std::size_t count = data_size / 4;
        w.samples.resize(count);
        std::memcpy(w.samples.data(), p + data_offset, count * sizeof(float));
    } else {
        return cfire::make_error(std::format("WAV decode failed: unsupported bits_per_sample {}", bits_per_sample));
    }
    return w;
}

[[nodiscard]] inline auto save_wav(const Wave& wave, const std::filesystem::path& path)
    -> std::expected<void, cfire::Error>
{
    auto bytes = encode_wav(wave);
    return cfire::fs::write_all(path, bytes);
}

[[nodiscard]] inline auto load_wav(const std::filesystem::path& path)
    -> std::expected<Wave, cfire::Error>
{
    auto bytes = cfire::fs::read_all(path);
    if (!bytes) return std::unexpected(bytes.error());
    return decode_wav(*bytes);
}

// ============================================================================
// Polyphonic Sound Handle (`audio::Sound`)
// ============================================================================

struct Sound {
    std::shared_ptr<detail::SoundData> data{};

    void play() const noexcept {
        if (!data) return;
        auto& eng = detail::engine();
        if (!eng.ready.load(std::memory_order_relaxed)) {
            (void)init();
        }
        {
            std::lock_guard<std::mutex> lock(eng.mtx);
            bool placed = false;
            for (auto& v : eng.voices) {
                if (!v.active) {
                    v.data = data;
                    v.cursor_frame = 0.0;
                    v.paused = false;
                    v.active = true;
                    placed = true;
                    break;
                }
            }
            if (!placed && eng.voices.size() < 64) {
                eng.voices.push_back(detail::ActiveVoice{data, 0.0, false, true});
            }
        }
        if (eng.headless.load(std::memory_order_relaxed)) {
            step_mixer(256);
        }
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
        detail::wasm_pump_audio();
#endif
    }

    void stop() const noexcept {
        if (!data) return;
        auto& eng = detail::engine();
        std::lock_guard<std::mutex> lock(eng.mtx);
        for (auto& v : eng.voices) {
            if (v.data == data) v.active = false;
        }
    }

    void pause() const noexcept {
        if (!data) return;
        auto& eng = detail::engine();
        std::lock_guard<std::mutex> lock(eng.mtx);
        for (auto& v : eng.voices) {
            if (v.data == data && v.active) v.paused = true;
        }
    }

    void resume() const noexcept {
        if (!data) return;
        auto& eng = detail::engine();
        std::lock_guard<std::mutex> lock(eng.mtx);
        for (auto& v : eng.voices) {
            if (v.data == data && v.active) v.paused = false;
        }
    }

    [[nodiscard]] auto is_playing() const noexcept -> bool {
        if (!data) return false;
        auto& eng = detail::engine();
        std::lock_guard<std::mutex> lock(eng.mtx);
        for (const auto& v : eng.voices) {
            if (v.data == data && v.active && !v.paused) return true;
        }
        return false;
    }

    void set_volume(float vol) const noexcept {
        if (data) data->volume.store(std::clamp(vol, 0.0f, 2.0f), std::memory_order_relaxed);
    }

    void set_pitch(float pitch) const noexcept {
        if (data) data->pitch.store(std::clamp(pitch, 0.125f, 4.0f), std::memory_order_relaxed);
    }

    void set_pan(float pan) const noexcept {
        if (data) data->pan.store(std::clamp(pan, -1.0f, 1.0f), std::memory_order_relaxed);
    }

    void set_looping(bool loop) const noexcept {
        if (data) data->looping.store(loop, std::memory_order_relaxed);
    }
};

[[nodiscard]] inline auto load_sound_from_wave(const Wave& wave) -> Sound {
    auto sd = std::make_shared<detail::SoundData>();
    sd->samples = wave.samples;
    sd->sample_rate = std::max(8000, wave.sample_rate);
    sd->channels = std::clamp(wave.channels, 1, 2);
    return Sound{std::move(sd)};
}

[[nodiscard]] inline auto load_sound(const std::filesystem::path& path)
    -> std::expected<Sound, cfire::Error>
{
    auto w = load_wav(path);
    if (!w) return std::unexpected(w.error());
    return load_sound_from_wave(*w);
}

// ============================================================================
// Real-Time Audio Stream (`audio::Stream`)
// ============================================================================

struct Stream {
    std::shared_ptr<detail::StreamState> state{};

    void push_samples(std::span<const float> samples) const noexcept {
        if (!state) return;
        state->push_interleaved(samples);
        auto& eng = detail::engine();
        if (eng.headless.load(std::memory_order_relaxed)) {
            eng.master_rms.store(state->rms.load(std::memory_order_relaxed), std::memory_order_relaxed);
            eng.master_peak.store(state->peak.load(std::memory_order_relaxed), std::memory_order_relaxed);
        }
#if defined(__EMSCRIPTEN__) || defined(PLATFORM_WEB)
        detail::wasm_pump_audio();
#endif
    }

    [[nodiscard]] auto queued_frames() const noexcept -> std::int32_t {
        return state ? state->queued_frames() : 0;
    }

    [[nodiscard]] auto needs_data(std::int32_t target_frames = 2048) const noexcept -> bool {
        return queued_frames() < target_frames;
    }

    [[nodiscard]] auto rms_level() const noexcept -> float {
        return state ? state->rms.load(std::memory_order_relaxed) : 0.0f;
    }

    [[nodiscard]] auto peak_level() const noexcept -> float {
        return state ? state->peak.load(std::memory_order_relaxed) : 0.0f;
    }

    void set_volume(float vol) const noexcept {
        if (state) state->volume.store(std::clamp(vol, 0.0f, 2.0f), std::memory_order_relaxed);
    }

    void play() const noexcept {
        if (state) state->playing.store(true, std::memory_order_relaxed);
    }

    void pause() const noexcept {
        if (state) state->playing.store(false, std::memory_order_relaxed);
    }

    void stop() const noexcept {
        if (state) {
            state->playing.store(false, std::memory_order_relaxed);
            state->clear();
        }
    }

    void close() const noexcept {
        if (state) {
            state->closed.store(true, std::memory_order_relaxed);
            state->clear();
        }
    }
};

[[nodiscard]] inline auto open_stream(std::int32_t sample_rate = 48000, std::int32_t channels = 2)
    -> std::expected<Stream, cfire::Error>
{
    auto& eng = detail::engine();
    if (!eng.ready.load(std::memory_order_relaxed)) {
        if (auto r = init(sample_rate, channels); !r) {
            return std::unexpected(r.error());
        }
    }
    auto st = std::make_shared<detail::StreamState>(sample_rate, channels);
    {
        std::lock_guard<std::mutex> lock(eng.mtx);
        eng.streams.push_back(st);
    }
    return Stream{std::move(st)};
}

} // namespace cfire::audio
