#include "playback_diagnostics.hpp"
#include "player.hpp"
#include "decoder.hpp"
#include "extended_audio.hpp"
#include "disc.hpp"
#include <deque>
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
struct PreparedAudio {
 std::unique_ptr<Input> input;std::unique_ptr<ma_decoder> decoder=std::make_unique<ma_decoder>();ExtendedAudio extra;TrackDetails details;std::string error;bool extended=false,live=false,initialized=false;double duration=0;
 ~PreparedAudio(){if(initialized&&!extended)ma_decoder_uninit(decoder.get());}
};
static std::shared_ptr<PreparedAudio> prepareAudio(Track track,const std::atomic<uint64_t> *generation,uint64_t gen){auto a=std::make_shared<PreparedAudio>();a->input=openInput(track.source,track.path,a->error,generation,gen);if(!a->input)return a;
 a->live=track.source.root==RadioRoot;auto ext=lower(filename(track.path));a->extended=a->live||(ext.size()>=4&&ext.substr(ext.size()-4)==".m4a");a->details=a->live?TrackDetails{}:readTrackDetails(*a->input,track.path,true);if(a->live){a->details.title=track.name;a->details.album="Online Radio";}if(*generation!=gen)return a;
 auto config=ma_decoder_config_init(ma_format_f32,2,44100);a->initialized=a->extended?a->extra.open(*a->input,a->live,a->details,a->error):ma_decoder_init(readDecoder,seekDecoder,a->input.get(),&config,a->decoder.get())==MA_SUCCESS;
 if(!a->initialized){if(a->error.empty())a->error="Unsupported or unreadable audio.";return a;}
 if(!a->extended){ma_uint32 rate=0,channels=0;if(a->decoder->pBackend&&ma_data_source_get_data_format(a->decoder->pBackend,nullptr,&channels,&rate,nullptr,0)==MA_SUCCESS){if(rate)a->details.sampleRate=rate;if(channels)a->details.channels=channels;}if(!a->details.sampleRate)a->details.sampleRate=a->decoder->converter.sampleRateIn;ma_uint64 length=0;if(ext.size()<4||ext.substr(ext.size()-4)!=".mp3")ma_decoder_get_length_in_pcm_frames(a->decoder.get(),&length);a->duration=length?length/44100.:a->details.duration;}else a->duration=a->live?0:a->details.duration;
 if(!a->details.bitRate&&a->duration>0)a->details.bitRate=uint32_t(a->input->size()*8./a->duration/1000.);return a;
}
Player::Player(const Settings &s) : settings_(s) {
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
void Player::metadata(const TrackDetails &details,const Source &source,const std::string &path){std::lock_guard<std::mutex> lock(mutex_);if(state_.track.path!=path||state_.track.source.root!=source.root||state_.track.source.share.server!=source.share.server||state_.track.source.share.share!=source.share.share)return;if(!details.title.empty())state_.details.title=details.title;if(!details.artist.empty())state_.details.artist=details.artist;if(!details.album.empty())state_.details.album=details.album;}
Playback Player::snapshot() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result=state_;
    if(device_&&(!audibleTracks_.empty()||trackStart_)){uint64_t queued=SDL_GetQueuedAudioSize(device_)/(2*sizeof(float)),audible=submittedTotal_>queued?submittedTotal_-queued:0;while(!audibleTracks_.empty()&&audibleTracks_.front().end<=audible)audibleTracks_.pop_front();if(!audibleTracks_.empty()){auto &old=audibleTracks_.front();result=old.playback;result.paused=state_.paused;result.position=std::max(0.,double(audible-old.start)/44100.);}else result.position=std::max(0.,double(audible>trackStart_?audible-trackStart_:0)/44100.);}

    if(!result.playing||result.busy||result.ended){result.spectrum={};result.waveLeft={};result.waveRight={};return result;}
    const uint64_t queued=device_?SDL_GetQueuedAudioSize(device_)/(2*sizeof(float)):0;
    auto visual=visualQueue_.sample(queued);
    result.spectrum=visual.spectrum;result.waveLeft=visual.left;result.waveRight=visual.right;result.analysisFrame=visual.frame;
    return result;
}
std::vector<Track> Player::queue() {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_;
}
void Player::requestLocked() {
    generation_++;handoff_=false;audibleTracks_.clear();submittedTotal_=trackStart_=0;
    pending_ = true;
    seek_ = -1;
    state_ = {};
    resetVisualsLocked();
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
    audibleTracks_.clear();submittedTotal_=trackStart_=0;
    state_.playing = state_.busy = state_.paused = state_.ended = false;
    state_.position = 0;
    resetVisualsLocked();
    state_.error.clear();
    if (device_) {
        SDL_PauseAudioDevice(device_, 1);
        SDL_ClearQueuedAudio(device_);
    }
}
void Player::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(state_.track.source.root==RadioRoot)return;
    if (!state_.playing && !state_.busy) {
        if (!queue_.empty())
            requestLocked();
        return;
    }
    state_.paused = !state_.paused;
    if (device_ && !state_.busy)
        SDL_PauseAudioDevice(device_, state_.paused);
}
int Player::audibleIndexLocked(){uint64_t queued=device_?SDL_GetQueuedAudioSize(device_)/(2*sizeof(float)):0,audible=submittedTotal_>queued?submittedTotal_-queued:0;while(!audibleTracks_.empty()&&audibleTracks_.front().end<=audible)audibleTracks_.pop_front();return audibleTracks_.empty()?index_:audibleTracks_.front().playback.index;}
void Player::next(bool manual) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
        return;
    if(manual)index_=audibleIndexLocked();
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
    index_=audibleIndexLocked();
    index_ = (index_ + queue_.size() - 1) % queue_.size();
    requestLocked();
}
void Player::seek(double seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(state_.track.source.root==RadioRoot)return;
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
    if (track.source.root == DiscRoot) { decodeDisc(std::move(track), gen); return; }
    auto fail = [&](const std::string &e) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation_ == gen) {
            state_.error = e;
            state_.busy = state_.playing = false;
            SDL_ClearQueuedAudio(device_);
        }
    };
    std::shared_ptr<PreparedAudio> audio;
    if(prepared_.valid()&&prepared_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto ready=prepared_.get();if(preparedTrack_.path==track.path&&preparedTrack_.source.root==track.source.root&&preparedTrack_.source.smb==track.source.smb&&preparedTrack_.source.share.server==track.source.share.server&&preparedTrack_.source.share.share==track.source.share.share&&preparedTrack_.source.share.folder==track.source.share.folder&&preparedTrack_.source.share.user==track.source.share.user&&ready->initialized){ready->input->resumeGeneration(&generation_,gen);audio=std::move(ready);}}
    if(!audio)audio=prepareAudio(track,&generation_,gen);
    if(generation_!=gen)return;
    if(!audio->initialized){fail(audio->error.empty()?"Cannot open audio.":audio->error);return;}
    auto &input=audio->input;auto &decoder=*audio->decoder;auto &extra=audio->extra;bool live=audio->live,extended=audio->extended;std::string error;ma_result result=MA_SUCCESS;double duration=audio->duration;
    bool continuing=false;int nextIndex=-1;Track nextTrack;
    {std::lock_guard<std::mutex> lock(mutex_);if(generation_!=gen)return;continuing=handoff_;handoff_=false;state_.duration=duration;state_.details=audio->details;
     if(settings_.gaplessPlayback&&!live){if(settings_.repeat==2)nextIndex=index_;else if(settings_.shuffle&&queue_.size()>1){nextIndex=(index_+1+SDL_GetTicks()%unsigned(queue_.size()-1))%int(queue_.size());}else if(index_+1<int(queue_.size()))nextIndex=index_+1;else if(settings_.repeat==1)nextIndex=0;
      if(nextIndex>=0){nextTrack=queue_[nextIndex];if(nextTrack.source.root==DiscRoot||nextTrack.source.root==RadioRoot)nextIndex=-1;}}
    }
    if(nextIndex>=0&&!prepared_.valid()){preparedTrack_=nextTrack;preparedGeneration_=gen;prepared_=std::async(std::launch::async,[this,nextTrack,gen]{return prepareAudio(nextTrack,&generation_,gen);});}
    Equalizer eq;
    AudioAnalysis analysis;
    std::vector<float> samples(2048 * 2);
    uint64_t decoded = 0;
    bool eof = false, started = continuing;
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
            if ((extended?extra.seek(seek):ma_decoder_seek_to_pcm_frame(&decoder, ma_uint64(seek * 44100)) == MA_SUCCESS)) {
                std::lock_guard<std::mutex> lock(mutex_);
                if (generation_ != gen)
                    return;
                SDL_ClearQueuedAudio(device_);
                decoded = uint64_t(seek * 44100);audibleTracks_.clear();submittedTotal_=decoded;trackStart_=0;
                eof = false;
                started = false;
                state_.busy = true;
                state_.position = seek;
                eq.reset();
                analysis.reset(decoded);resetVisualsLocked(decoded);
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
        if(started&&nextIndex>=0&&prepared_.valid()&&preparedGeneration_!=gen&&prepared_.wait_for(std::chrono::milliseconds(0))==std::future_status::ready){auto stale=prepared_.get();preparedTrack_=nextTrack;preparedGeneration_=gen;prepared_=std::async(std::launch::async,[this,nextTrack,gen,stale=std::move(stale)]()mutable{stale.reset();return prepareAudio(nextTrack,&generation_,gen);});}
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
                    state_.spectrum = {};state_.waveLeft={};state_.waveRight={};
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
        if(extended){frames=extra.read(samples.data(),2048,error);result=error.empty()?(frames?MA_SUCCESS:MA_AT_END):MA_ERROR;}
        else result=ma_decoder_read_pcm_frames(&decoder,samples.data(),2048,&frames);
        if(live){auto title=input->streamTitle();if(!title.empty()){std::lock_guard<std::mutex> lock(mutex_);if(generation_==gen){state_.details.title=title;state_.details.album=track.name;}}}
        if (firstRead)
            playbackTrace("decoder.pcm.end", frames);
        if (generation_ != gen)
            return;
        if (frames == 0) {
            if(live&&!quitting_&&generation_==gen){
                {std::lock_guard<std::mutex> lock(mutex_);if(generation_==gen){state_.busy=true;state_.playing=false;state_.position=0;state_.error="Reconnecting to radio...";resetVisualsLocked();SDL_ClearQueuedAudio(device_);SDL_PauseAudioDevice(device_,1);}}
                for(int wait=0;wait<20&&!quitting_&&generation_==gen;wait++)SDL_Delay(100);
                if(!quitting_&&generation_==gen){std::lock_guard<std::mutex> lock(mutex_);if(generation_==gen)pending_=true;}
                return;
            }
            if(!live&&decoded>0&&nextIndex>=0&&prepared_.valid()&&preparedGeneration_==gen&&preparedTrack_.path==nextTrack.path&&input->error().empty()){
                std::lock_guard<std::mutex> lock(mutex_);
                if(generation_==gen&&settings_.gaplessPlayback){audibleTracks_.push_back({state_,trackStart_,submittedTotal_});trackStart_=submittedTotal_;index_=nextIndex;pending_=true;handoff_=true;state_={};resetVisualsLocked();state_.track=queue_[index_];state_.index=index_;state_.count=queue_.size();state_.playing=true;return;}
            }
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
        auto visualFrames=analysis.push(samples.data(),frames); // Post-EQ, before user volume.
        float volume = settings.volume / 100.f;
        for (size_t i = 0; i < frames * 2; i++)
            samples[i] *= volume;
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
            decoded += frames;submittedTotal_+=frames;
            state_.position = std::max(
                0.,
                (double(decoded) - SDL_GetQueuedAudioSize(device_) / (2 * sizeof(float))) / 44100.);
            publishVisualsLocked(std::move(visualFrames),decoded);
            if (!started) {
                started = true;
                state_.busy = false;state_.error.clear();
                state_.playing = true;
                SDL_PauseAudioDevice(device_, state_.paused);
            }
        }
    }
}
#include "player_disc.inc"
} // namespace nxh
