#include "extended_audio.hpp"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
}
namespace nxh {
struct ExtendedAudio::Impl {
    Input *input=nullptr;AVFormatContext *format=nullptr;AVIOContext *io=nullptr;AVCodecContext *codec=nullptr;
    SwrContext *swr=nullptr;AVPacket *packet=av_packet_alloc();AVFrame *frame=av_frame_alloc();int stream=-1;
    double discardUntil=-1;
    bool live=false,eof=false,flushed=false;std::vector<float> pcm;size_t at=0;
    ~Impl(){swr_free(&swr);avcodec_free_context(&codec);avformat_close_input(&format);if(io){av_freep(&io->buffer);avio_context_free(&io);}av_frame_free(&frame);av_packet_free(&packet);}
    static int readIO(void *p,uint8_t *out,int n){auto &s=*static_cast<Impl*>(p);size_t got=s.input->read(out,n);return got?int(got):s.input->error().empty()?AVERROR_EOF:AVERROR(EIO);}
    static int64_t seekIO(void *p,int64_t off,int whence){auto &s=*static_cast<Impl*>(p);if(whence==AVSEEK_SIZE)return s.input->size();if(s.live)return AVERROR(ENOSYS);return s.input->seek(off,whence&~AVSEEK_FORCE)?int64_t(s.input->position()):AVERROR(EIO);}
};
ExtendedAudio::ExtendedAudio():impl(new Impl){} ExtendedAudio::~ExtendedAudio()=default;
bool ExtendedAudio::open(Input &input,bool live,TrackDetails &m,std::string &error){
 auto &s=*impl;s.input=&input;s.live=live;s.format=avformat_alloc_context();auto *buffer=(uint8_t*)av_malloc(32768);
 if(!s.format||!buffer||!s.packet||!s.frame){av_free(buffer);error="Cannot allocate audio decoder.";return false;}
 s.io=avio_alloc_context(buffer,32768,0,&s,Impl::readIO,nullptr,live?nullptr:Impl::seekIO);if(!s.io){av_free(buffer);error="Cannot allocate audio input.";return false;}
 s.io->seekable=live?0:AVIO_SEEKABLE_NORMAL;s.format->pb=s.io;s.format->flags|=AVFMT_FLAG_CUSTOM_IO;s.format->probesize=live?16384:65536;s.format->max_analyze_duration=live?250000:2000000;
 int result=avformat_open_input(&s.format,nullptr,nullptr,nullptr);if(result>=0){int candidate=av_find_best_stream(s.format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);bool inspect=live||candidate<0;if(!inspect){auto *parameters=s.format->streams[candidate]->codecpar;inspect=parameters->sample_rate<=0||parameters->ch_layout.nb_channels<=0||parameters->codec_id==AV_CODEC_ID_NONE||parameters->extradata_size<=0;}if(inspect)result=avformat_find_stream_info(s.format,nullptr);}
 if(result<0){error="Unrecognized or unavailable audio stream.";return false;}
 s.stream=av_find_best_stream(s.format,AVMEDIA_TYPE_AUDIO,-1,-1,nullptr,0);if(s.stream<0){error="No supported audio track.";return false;}
 auto *stream=s.format->streams[s.stream];auto *decoder=avcodec_find_decoder(stream->codecpar->codec_id);
 if(!decoder){error="Unsupported stream codec.";return false;}s.codec=avcodec_alloc_context3(decoder);if(!s.codec){error="Cannot allocate decoder.";return false;}
 avcodec_parameters_to_context(s.codec,stream->codecpar);s.codec->thread_count=1;if(avcodec_open2(s.codec,decoder,nullptr)<0){error="Cannot initialize audio codec.";return false;}
 AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
 if(swr_alloc_set_opts2(&s.swr,&stereo,AV_SAMPLE_FMT_FLT,44100,&s.codec->ch_layout,s.codec->sample_fmt,s.codec->sample_rate,0,nullptr)<0||swr_init(s.swr)<0){error="Unsupported audio layout.";return false;}
 m.sampleRate=s.codec->sample_rate;m.channels=s.codec->ch_layout.nb_channels;m.bitDepth=s.codec->bits_per_raw_sample;
 m.format=stream->codecpar->codec_id==AV_CODEC_ID_ALAC?"ALAC":stream->codecpar->codec_id==AV_CODEC_ID_AAC?"AAC":avcodec_get_name(stream->codecpar->codec_id);
 m.bitRate=stream->codecpar->bit_rate/1000;if(!live){if(s.format->duration>0)m.duration=s.format->duration/double(AV_TIME_BASE);else if(stream->duration>0)m.duration=stream->duration*av_q2d(stream->time_base);}
 auto text=[&](const char *key,std::string &out){auto *v=av_dict_get(s.format->metadata,key,nullptr,0);if(!v)v=av_dict_get(stream->metadata,key,nullptr,0);if(v)out=v->value;};
 text("title",m.title);text("artist",m.artist);text("album",m.album);return true;
}
size_t ExtendedAudio::read(float *out,size_t wanted,std::string &error){auto &s=*impl;size_t done=0;
 while(done<wanted){if(s.at<s.pcm.size()){size_t n=std::min((wanted-done)*2,s.pcm.size()-s.at);std::copy_n(s.pcm.data()+s.at,n,out+done*2);s.at+=n;done+=n/2;continue;}
  s.pcm.clear();s.at=0;int r=avcodec_receive_frame(s.codec,s.frame);
  if(r>=0){int capacity=swr_get_out_samples(s.swr,s.frame->nb_samples);s.pcm.resize(size_t(capacity)*2);uint8_t *dst=(uint8_t*)s.pcm.data();int n=swr_convert(s.swr,&dst,capacity,(const uint8_t**)s.frame->extended_data,s.frame->nb_samples);double start=s.frame->best_effort_timestamp==AV_NOPTS_VALUE?-1:s.frame->best_effort_timestamp*av_q2d(s.format->streams[s.stream]->time_base);av_frame_unref(s.frame);if(n<0){error="Audio conversion failed.";break;}s.pcm.resize(size_t(n)*2);if(s.discardUntil>=0&&start>=0){size_t skip=size_t(std::clamp((s.discardUntil-start)*44100.,0.,double(n)));s.at=skip*2;if(skip<size_t(n))s.discardUntil=-1;}continue;}
  if(r==AVERROR_EOF){if(!s.flushed){s.flushed=true;int cap=swr_get_out_samples(s.swr,0);s.pcm.resize(size_t(cap)*2);uint8_t *dst=(uint8_t*)s.pcm.data();int n=swr_convert(s.swr,&dst,cap,nullptr,0);s.pcm.resize(size_t(std::max(n,0))*2);if(n>0)continue;}break;}
  if(r!=AVERROR(EAGAIN)){error="Audio decoding failed.";break;}
  if(s.eof)break;
  do{r=av_read_frame(s.format,s.packet);if(r<0){s.eof=true;avcodec_send_packet(s.codec,nullptr);break;}if(s.packet->stream_index!=s.stream){av_packet_unref(s.packet);continue;}r=avcodec_send_packet(s.codec,s.packet);av_packet_unref(s.packet);break;}while(true);
 }
 return done;
}
std::vector<unsigned char> ExtendedAudio::artwork()const{auto &s=*impl;if(!s.format)return {};for(unsigned i=0;i<s.format->nb_streams;i++){auto &packet=s.format->streams[i]->attached_pic;if(packet.size>0&&packet.size<=8*1024*1024)return {packet.data,packet.data+packet.size};}return {};}
bool ExtendedAudio::seek(double seconds){auto &s=*impl;if(s.live)return false;auto *stream=s.format->streams[s.stream];if(av_seek_frame(s.format,s.stream,int64_t(seconds/av_q2d(stream->time_base)),AVSEEK_FLAG_BACKWARD)<0)return false;avcodec_flush_buffers(s.codec);swr_close(s.swr);swr_init(s.swr);s.pcm.clear();s.at=0;s.eof=s.flushed=false;s.discardUntil=seconds;
 // Decode forward from the preceding packet timestamp to the requested position.
 avformat_flush(s.format);return true;}
}
