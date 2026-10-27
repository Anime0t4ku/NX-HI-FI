#include "playback_diagnostics.hpp"
#include "player.hpp"
#include "decoder.hpp"
#include <random>
namespace nxh {
static ma_result readDecoder(ma_decoder *d, void *out, size_t n, size_t *got) {
    auto *i = static_cast<Input *>(d->pUserData);
    *got = i->read(out, n);
    return *got ? MA_SUCCESS : i->error().empty() ? MA_AT_END : MA_IO_ERROR;
}
static ma_result seekDecoder(ma_decoder *d, ma_int64 offset, ma_seek_origin origin) {
    return static_cast<Input *>(d->pUserData)
                   ->seek(offset, origin == ma_seek_origin_start     ? SEEK_SET
                                  : origin == ma_seek_origin_current ? SEEK_CUR
                                                                     : SEEK_END)
               ? MA_SUCCESS
               : MA_BAD_SEEK;
}
Player::Player(const Settings &s) : settings_(s), cosines_(32), sines_(32) {
    SDL_AudioSpec wanted{};
    wanted.freq = 44100;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = 2;
    wanted.samples = 1024;
    device_ = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
    if (!device_) {
        state_.error = SDL_GetError();
        return;
    }
    for (int b = 0; b < 32; b++)
        for (int i = 0; i < 256; i++) {
            double phase = 2 * 3.141592653589793 * (b + 1) * i / 256.;
            double window = .5 - .5 * std::cos(2 * 3.141592653589793 * i / 255.);
            cosines_[b][i] = std::cos(phase) * window;
            sines_[b][i] = std::sin(phase) * window;
        }
    worker_ = std::thread(&Player::work, this);
}
Player::~Player() {
    quitting_ = true;
    generation_++;
    condition_.notify_all();
    if (worker_.joinable())
        worker_.join();
    if (device_)
        SDL_CloseAudioDevice(device_);
}
void Player::settings(const Settings &s) {
    std::lock_guard<std::mutex> lock(mutex_);
    settings_ = s;
}
Playback Player::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}
std::vector<Track> Player::queue() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_;
}
void Player::requestLocked() {
    generation_++;
    pending_ = true;
    seek_ = -1;
    state_ = {};
    state_.track = queue_[index_];
    state_.index = index_;
    state_.count = queue_.size();
    state_.busy = true;
    if (device_) {
        SDL_PauseAudioDevice(device_, 1);
        SDL_ClearQueuedAudio(device_);
    }
    condition_.notify_one();
}
void Player::play(std::vector<Track> q, int index) {
    playbackTrace("player.queue.begin", q.size());
    std::lock_guard<std::mutex> lock(mutex_);
    if (!device_ || q.empty() || index < 0 || index >= int(q.size()))
        return;
    queue_ = std::move(q);
    playbackTrace("player.queue.committed");
    index_ = index;
    requestLocked();
    playbackTrace("player.queue.ready");
}
void Player::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    generation_++;
    pending_ = false;
    seek_ = -1;
    state_.playing = state_.busy = state_.paused = state_.ended = false;
    state_.position = 0;
    state_.spectrum = {};
    state_.error.clear();
    if (device_) {
        SDL_PauseAudioDevice(device_, 1);
        SDL_ClearQueuedAudio(device_);
    }
}
void Player::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.playing && !state_.busy) {
        if (!queue_.empty())
            requestLocked();
        return;
    }
    state_.paused = !state_.paused;
    if (device_ && !state_.busy)
        SDL_PauseAudioDevice(device_, state_.paused);
}
void Player::next(bool manual) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
        return;
    int target = index_;
    if (!manual && settings_.repeat == 2) {
    } else if (settings_.shuffle && queue_.size() > 1) {
        static std::mt19937 rng(SDL_GetTicks());
        std::uniform_int_distribution<int> d(0, queue_.size() - 2);
        target = d(rng);
        if (target >= index_)
            target++;
    } else if (index_ + 1 < int(queue_.size()))
        target++;
    else if (manual || settings_.repeat == 1)
        target = 0;
    else {
        state_.ended = false;
        return;
    }
    index_ = target;
    requestLocked();
}
void Player::previous() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
        return;
    index_ = (index_ + queue_.size() - 1) % queue_.size();
    requestLocked();
}
void Player::seek(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!state_.playing && !state_.busy)
        return;
    seek_ = std::max(0., (seek_ >= 0 ? seek_ : state_.position) + seconds);
    if (state_.duration > 0)
        seek_ = std::min(seek_, std::max(0., state_.duration - .05));
}
void Player::tick() {
    bool ended;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ended = state_.ended;
    }
    if (ended)
        next(false);
}
void Player::work() {
    while (!quitting_) {
        Track track;
        uint64_t gen;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            condition_.wait(lock, [&] { return quitting_ || pending_; });
            if (quitting_)
                return;
            track = queue_[index_];
            gen = generation_;
            pending_ = false;
        }
        decode(track, gen);
    }
}
void Player::decode(Track track, uint64_t gen) {
    auto fail = [&](const std::string &e) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_ == gen) {
            state_.error = e;
            state_.busy = state_.playing = false;
            SDL_ClearQueuedAudio(device_);
        }
    };
    playbackTrace("decoder.input.begin");
    std::string error;
    auto input = openInput(track.source, track.path, error, &generation_, gen);
    playbackTrace("decoder.input.end", bool(input));
    if (!input) {
        fail(error);
        return;
    }
    if (generation_ != gen)
        return;
    playbackTrace("decoder.metadata.begin");
    auto details = readTrackDetails(*input, track.path);
    playbackTrace("decoder.metadata.end", details.sampleRate);
    if (generation_ != gen)
        return;
    ma_decoder decoder{};
    auto config = ma_decoder_config_init(ma_format_f32, 2, 44100);
    playbackTrace("decoder.init.begin");
    ma_result result = ma_decoder_init(readDecoder, seekDecoder, input.get(), &config, &decoder);
    playbackTrace("decoder.init.end", result);
    if (result != MA_SUCCESS) {
        fail("Unsupported or unreadable audio file. Supported formats: WAV, FLAC, MP3 and Ogg "
             "Vorbis.");
        return;
    }
    struct Guard {
        ma_decoder *d;
        ~Guard() { ma_decoder_uninit(d); }
    } guard{&decoder};
    ma_uint32 nativeRate = 0, nativeChannels = 0;
    if (decoder.pBackend &&
        ma_data_source_get_data_format(decoder.pBackend, nullptr, &nativeChannels, &nativeRate,
                                       nullptr, 0) == MA_SUCCESS) {
        if (nativeRate)
            details.sampleRate = nativeRate;
        if (nativeChannels)
            details.channels = nativeChannels;
    }
    if (!details.sampleRate)
        details.sampleRate = decoder.converter.sampleRateIn;
    ma_uint64 length = 0;
    auto ext = lower(track.path.substr(track.path.find_last_of('.')));
    if (ext != ".mp3")
        ma_decoder_get_length_in_pcm_frames(&decoder, &length);
    double duration = length ? length / 44100. : details.duration;
    if (!details.bitRate && duration > 0) {
        if (details.format == "WAV" && details.sampleRate && details.channels && details.bitDepth)
            details.bitRate =
                uint64_t(details.sampleRate) * details.channels * details.bitDepth / 1000;
        else
            details.bitRate = uint32_t(input->size() * 8. / duration / 1000.);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_ != gen)
            return;
        state_.duration = duration;
        state_.details = std::move(details);
    }
    Equalizer eq;
    std::vector<float> samples(2048 * 2);
    uint64_t decoded = 0;
    bool eof = false, started = false;
    bool firstRead = true;
    while (!quitting_ && generation_ == gen) {
        Settings settings;
        double seek = -1;
        bool paused;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            settings = settings_;
            paused = state_.paused;
            seek = seek_;
            seek_ = -1;
        }
        if (seek >= 0) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ != gen)
                    return;
                state_.busy = true;
            }
            SDL_PauseAudioDevice(device_, 1);
            if (ma_decoder_seek_to_pcm_frame(&decoder, ma_uint64(seek * 44100)) == MA_SUCCESS) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ != gen)
                    return;
                SDL_ClearQueuedAudio(device_);
                decoded = uint64_t(seek * 44100);
                eof = false;
                started = false;
                state_.busy = true;
                state_.position = seek;
                eq.reset();
            } else {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ == gen) {
                    state_.busy = false;
                    state_.error = "Seeking is unavailable for this file.";
                    playbackTrace("audio.start.begin");
                    SDL_PauseAudioDevice(device_, state_.paused);
                    playbackTrace("audio.start.end");
                }
            }
        }
        auto queued = SDL_GetQueuedAudioSize(device_);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation_ != gen)
                return;
            state_.position =
                std::max(0., (double(decoded) - queued / (2 * sizeof(float))) / 44100.);
        }
        if (started && paused) {
            SDL_Delay(10);
            continue;
        }
        if (eof) {
            if (queued == 0) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ == gen) {
                    state_.playing = false;
                    state_.ended = true;
                    state_.position = decoded / 44100.;
                    state_.spectrum = {};
                }
                return;
            }
            SDL_Delay(10);
            continue;
        }
        if (queued > 44100 * 2 * sizeof(float) / 2) {
            SDL_Delay(10);
            continue;
        }
        ma_uint64 frames = 0;
        if (firstRead)
            playbackTrace("decoder.pcm.begin");
        result = ma_decoder_read_pcm_frames(&decoder, samples.data(), 2048, &frames);
        if (firstRead)
            playbackTrace("decoder.pcm.end", frames);
        if (generation_ != gen)
            return;
        if (frames == 0) {
            if (!input->error().empty()) {
                fail(input->error());
                return;
            }
            if (result != MA_SUCCESS && result != MA_AT_END) {
                fail("Audio decoding failed.");
                return;
            }
            eof = true;
            continue;
        }
        eq.configure(settings.eqEnabled, settings.eq);
        eq.process(samples.data(), frames);
        float volume = settings.volume / 100.f;
        for (size_t i = 0; i < frames * 2; i++)
            samples[i] *= volume;
        std::array<float, 32> spectrum{};
        if (frames >= 256 && settings.visualizer)
            for (int b = 0; b < 32; b++) {
                double re = 0, im = 0;
                for (int i = 0; i < 256; i++) {
                    float mono = (samples[i * 2] + samples[i * 2 + 1]) * .5f;
                    re += mono * cosines_[b][i];
                    im += mono * sines_[b][i];
                }
                float db = 20 * std::log10(std::max(1e-6, std::sqrt(re * re + im * im) / 64.));
                spectrum[b] = std::clamp((db + 60) / 60., 0., 1.);
            }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation_ != gen)
                return;
            if (firstRead)
                playbackTrace("audio.queue.begin");
            if (SDL_QueueAudio(device_, samples.data(), frames * 2 * sizeof(float)) != 0) {
                state_.error = "Could not queue audio.";
                state_.playing = state_.busy = false;
                return;
            }
            if (firstRead) {
                playbackTrace("audio.queue.end");
                firstRead = false;
            }
            decoded += frames;
            state_.position = std::max(
                0.,
                (double(decoded) - SDL_GetQueuedAudioSize(device_) / (2 * sizeof(float))) / 44100.);
            state_.spectrum = spectrum;
            if (!started) {
                started = true;
                state_.busy = false;
                state_.playing = true;
                SDL_PauseAudioDevice(device_, state_.paused);
            }
        }
    }
}
} // namespace nxh
