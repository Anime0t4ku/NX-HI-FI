// Included inside namespace nxh by player.cpp.
void Player::decodeDisc(Track track, uint64_t gen) {
    auto fail = [&](const std::string &error) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_ == gen) {
            state_.error = error;
            state_.playing = state_.busy = state_.ended = false;
            SDL_ClearQueuedAudio(device_);
        }
    };
    std::string error;
    auto input = openDisc(track.path, error, &generation_, gen);
    if (!input) { fail(error); return; }
    if (!input->seek(44, SEEK_SET)) { fail("Cannot start audio CD track."); return; }
    auto details = discDetails(track.path);
    details.duration = (input->size() - 44) / 176400.;
    int producingIndex;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_ != gen) return;
        producingIndex = index_;
        state_.details = details;
        state_.duration = details.duration;
    }
    struct Transition { uint64_t frame; int index; Track track; TrackDetails details; };
    std::deque<Transition> transitions;
    uint64_t decoded = 0, origin = 0;
    bool eof = false, started = false;
    Equalizer eq;
    AudioAnalysis analysis;
    std::mt19937 rng(SDL_GetTicks());
    std::vector<unsigned char> pcm(2048 * 4);
    std::vector<float> samples(2048 * 2);
    while (!quitting_ && generation_ == gen) {
        Settings settings;
        double seek;
        bool paused;
        Track audibleTrack;
        int audibleIndex;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            settings = settings_;
            paused = state_.paused;
            seek = seek_; seek_ = -1;
            audibleTrack = state_.track;
            audibleIndex = index_;
        }
        if (seek >= 0) {
            // Read-ahead can already be in the next track; seek the audible track.
            auto next = openDisc(audibleTrack.path, error, &generation_, gen);
            if (!next) { fail(error); return; }
            auto frames = uint64_t(seek * 44100.);
            frames = std::min(frames, (next->size() - 44) / 4);
            if (!next->seek(44 + int64_t(frames * 4), SEEK_SET)) {
                fail("Could not seek audio CD track."); return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ != gen) return;
                SDL_PauseAudioDevice(device_, 1);
                SDL_ClearQueuedAudio(device_);
                state_.busy = true;
                state_.position = frames / 44100.;
                resetVisualsLocked(frames);
            }
            input = std::move(next);track = audibleTrack;producingIndex = audibleIndex;
            decoded = frames;origin = 0;transitions.clear();eof = started = false;eq.reset();analysis.reset(decoded);
        }
        auto queued = SDL_GetQueuedAudioSize(device_);
        uint64_t audible = decoded - std::min<uint64_t>(decoded, queued / (2 * sizeof(float)));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation_ != gen) return;
            while (!transitions.empty() && audible >= transitions.front().frame) {
                auto t = std::move(transitions.front());transitions.pop_front();
                origin = t.frame;index_ = t.index;
                state_.track = std::move(t.track);state_.details = std::move(t.details);
                state_.index = index_;state_.duration = state_.details.duration;
            }
            state_.position = std::min(state_.duration, (audible - std::min(audible, origin)) / 44100.);
        }
        if (started && paused) { SDL_Delay(10); continue; }
        if (eof) {
            if (!started && queued) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ != gen) return;
                started = true;state_.busy = false;state_.playing = true;
                SDL_PauseAudioDevice(device_, state_.paused);
            }
            if (!queued) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ == gen) {
                    state_.playing = state_.busy = false;state_.ended = true;
                    state_.position = state_.duration;state_.spectrum = {};state_.waveLeft={};state_.waveRight={};
                }
                return;
            }
            SDL_Delay(10);continue;
        }
        if (queued > 44100 * 2 * sizeof(float) / 2) { SDL_Delay(10); continue; }
        size_t bytes = input->read(pcm.data(), pcm.size());
        if (generation_ != gen) return;
        if (!input->error().empty()) { fail(input->error()); return; }
        if (!bytes) {
            int nextIndex = -1;
            Track nextTrack;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ != gen) return;
                if (settings.gaplessPlayback) {
                    if (settings.repeat == 2) nextIndex = producingIndex;
                    else if (settings.shuffle && queue_.size() > 1) {
                        std::uniform_int_distribution<int> d(0, int(queue_.size()) - 2);
                        nextIndex = d(rng);if (nextIndex >= producingIndex) ++nextIndex;
                    } else if (producingIndex + 1 < int(queue_.size())) nextIndex = producingIndex + 1;
                    else if (settings.repeat == 1) nextIndex = 0;
                    if (nextIndex >= 0) nextTrack = queue_[nextIndex];
                }
            }
            if (nextIndex < 0 || nextTrack.source.root != DiscRoot) { eof = true; continue; }
            auto next = openDisc(nextTrack.path, error, &generation_, gen);
            if (!next) { fail(error); return; }
            if (!next->seek(44, SEEK_SET)) { fail("Cannot read next audio CD track."); return; }
            auto nextDetails = discDetails(nextTrack.path);
            nextDetails.duration = (next->size() - 44) / 176400.;
            transitions.push_back({decoded, nextIndex, nextTrack, nextDetails});
            input = std::move(next);track = std::move(nextTrack);producingIndex = nextIndex;
            // Preserve queued PCM and EQ state across the boundary: no stop/restart or silence.
            continue;
        }
        if (bytes % 4) { fail("Incomplete audio CD sample frame."); return; }
        size_t frames = bytes / 4;
        for (size_t i = 0; i < frames * 2; ++i) {
            int16_t value = int16_t(uint16_t(pcm[i*2]) | (uint16_t(pcm[i*2+1]) << 8));
            samples[i] = value / 32768.f;
        }
        eq.configure(settings.eqEnabled, settings.eq);
        eq.process(samples.data(), frames);
        auto visualFrames=analysis.push(samples.data(),frames);
        float volume = settings.volume / 100.f;
        for (size_t i = 0; i < frames * 2; ++i) samples[i] *= volume;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (generation_ != gen) return;
            if (SDL_QueueAudio(device_, samples.data(), frames * 2 * sizeof(float)) != 0) {
                state_.error = "Could not queue CD audio.";state_.playing = state_.busy = false;return;
            }
            decoded += frames;publishVisualsLocked(std::move(visualFrames),decoded);
            if (!started && SDL_GetQueuedAudioSize(device_) >= 44100 * 2 * sizeof(float) * 0.35) {
                started = true;state_.busy = false;state_.playing = true;
                SDL_PauseAudioDevice(device_, state_.paused);
            }
        }
    }
}
