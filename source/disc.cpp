#include "disc.hpp"
#include "disc_input.hpp"
#include <cstring>
#include <chrono>
#include <fstream>
#ifdef __SWITCH__
#include <usbdvd.h>
#include "cover_image.hpp"
#include "usbdvd_scsi.h"
#include <curl/curl.h>
#endif
namespace nxh {
namespace {
std::mutex deviceMutex, catalogMutex;
struct Album { DiscTOC toc; DiscMetadata meta; };
std::map<std::string,Album> catalog;
#ifdef __SWITCH__
struct Drive {
    std::unique_ptr<CSWITCH_USB> usb;
    std::unique_ptr<CUSBSCSI> scsi;
    std::mutex io;
    Drive() {
        usb=std::make_unique<CSWITCH_USB>(true,false);
        if(!usb->device_found)return;
        scsi=std::make_unique<CUSBSCSI>(usb.get());
        ScsiInquiryStandardData info{};
        if(scsi->UsbDvdSendInquiry(0,sizeof info,reinterpret_cast<uint8_t*>(&info))!=0||
           info.peripheral_device_type!=5){scsi.reset();return;}
        if(!std::strncmp(info.vendor_id,"Apple",5)&&!std::strncmp(info.product_id,"SuperDrive",10))
            scsi->AppleSuperDriveUnlock(0,0);
    }
    bool toc(DiscTOC &t,std::string &error) {
        std::lock_guard<std::mutex> lock(io);
        if(!scsi){error="No USB optical drive found. Connect a powered USB CD/DVD drive and retry.";return false;}
        // Unit attention after an insertion is cleared by the first readiness command.
        if(scsi->UsbDvdUnitReady(0)!=0) {
            ScsiRequestSenseDataFixedFormat sense{};
            scsi->UsbDvdSense(0,sizeof sense,&sense);
            if(scsi->UsbDvdUnitReady(0)!=0) {
                error="Drive is not ready. Insert an audio CD and retry.";return false;
            }
        }
        void *raw=nullptr;int size=0;
        int result=scsi->UsbDvdSendTOC(0,&raw,&size);
        std::unique_ptr<void,decltype(&std::free)> guard(raw,&std::free);
        if(result!=0){error="Could not read the disc table of contents.";return false;}
        return parseDiscTOC(static_cast<unsigned char*>(raw),size,t,error);
    }
    bool read(int lba,int count,unsigned char *out) {
        std::lock_guard<std::mutex> lock(io);
        // Do not retry unit attention: the inserted disc may have changed.
        return scsi&&scsi->UsbDvdUnitReady(0)==0&&scsi->UsbDvdReadCD_Audio(0,lba,count,out)==0;
    }
};
std::shared_ptr<Drive> drive;
std::shared_ptr<Drive> getDrive(bool refresh=false) {
    std::lock_guard<std::mutex> lock(deviceMutex);
    // Never invalidate a live input. Recreate between scans to handle unplug/replug.
    if(refresh&&drive&&drive.use_count()==1)drive.reset();
    if(!drive)drive=std::make_shared<Drive>();
    return drive;
}
struct Download { std::vector<unsigned char> *bytes;size_t limit; };
size_t receive(void *p,size_t size,size_t n,void *user) {
    auto &d=*static_cast<Download*>(user);
    if(size&&n>SIZE_MAX/size)return 0;
    size_t count=size*n;
    if(count>d.limit-d.bytes->size())return 0;
    auto *b=static_cast<unsigned char*>(p);d.bytes->insert(d.bytes->end(),b,b+count);return count;
}
DiscHTTP http() {
    static std::once_flag init;
    std::call_once(init,[]{curl_global_init(CURL_GLOBAL_DEFAULT);});
    auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(12);
    return [deadline](const std::string &url,std::vector<unsigned char> &out,size_t limit) {
        out.clear();
        long remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        if(remaining<=0)return 0;
        CURL *c=curl_easy_init();if(!c)return 0;
        Download data{&out,limit};
        curl_easy_setopt(c,CURLOPT_URL,url.c_str());
        curl_easy_setopt(c,CURLOPT_USERAGENT,"NX-Hi-Fi/1.2.0 (https://github.com/Anime0t4ku/NX-HI-FI)");
        curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
        curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
        curl_easy_setopt(c,CURLOPT_PROTOCOLS,CURLPROTO_HTTPS);
        curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS,CURLPROTO_HTTPS);
        curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
        curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT_MS,std::min(remaining,4000L));
        curl_easy_setopt(c,CURLOPT_TIMEOUT_MS,std::min(remaining,8000L));
        // Use the Switch system trust store through switch-curl's TLS backend.
        curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,receive);
        curl_easy_setopt(c,CURLOPT_WRITEDATA,&data);
        auto result=curl_easy_perform(c);long status=0;
        if(result==CURLE_OK)curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&status);
        curl_easy_cleanup(c);return int(status);
    };
}

#endif
bool pathTrack(const std::string &path,std::string &id,int &number) {
    auto slash=path.find('/');if(slash!=28||path.size()!=40)return false;
    id=path.substr(0,slash);
    if(id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._-")!=std::string::npos)return false;
    auto name=path.substr(slash+1);
    if(name.substr(0,5)!="Track"||name.substr(7)!=".wav"||!std::isdigit(name[5])||!std::isdigit(name[6]))return false;
    number=(name[5]-'0')*10+name[6]-'0';return number>=1&&number<=99;
}
} // namespace
bool listDisc(std::vector<Entry> &out,std::string &error) {
    out.clear();
#ifdef __SWITCH__
    auto d=getDrive(true);DiscTOC toc;
    if(!d->toc(toc,error))return false;
    auto id=discID(toc);
    auto meta=lookupDiscMetadata(id,"sdmc:/switch/nx-hifi/cd-cache",http(),[](const auto &bytes) {
        auto *surface=boundedCoverSurface(bytes);
        if(!surface)return false;
        SDL_FreeSurface(surface);return true;
    });
    // Check again after network lookup; do not list a disc that was swapped meanwhile.
    DiscTOC check;
    if(!d->toc(check,error)||discID(check)!=id){error="Disc changed. Open Physical Disc again.";return false;}
    for(auto &t:toc.audio) {
        char filename[32];std::snprintf(filename,sizeof filename,"Track%02d.wav",t.number);
        char label[32];std::snprintf(label,sizeof label,"Track %02d",t.number);
        auto title=meta.titles[t.number];if(title.empty())title=label;
        out.push_back({title,id+"/"+filename,false});
    }
    std::lock_guard<std::mutex> lock(catalogMutex);
    catalog[id]={std::move(toc),std::move(meta)};
    // Keep only a few discs in memory; the persistent cache retains all of them.
    while(catalog.size()>8){auto it=catalog.begin();if(it->first==id)++it;catalog.erase(it);}
    return true;
#else
    error="USB optical drives require a Nintendo Switch build.";return false;
#endif
}
std::unique_ptr<Input> openDisc(const std::string &path,std::string &error,const std::atomic<uint64_t> *g,uint64_t e) {
    std::string id;int number;
    if(!pathTrack(path,id,number)){error="Invalid audio CD track.";return {};}
#ifdef __SWITCH__
    auto d=getDrive();DiscTOC toc;
    if(!d->toc(toc,error))return {};
    if(discID(toc)!=id){error="Disc changed. Open Physical Disc again.";return {};}
    for(auto &t:toc.audio)if(t.number==number)return std::make_unique<CDDAInput>([d](int lba,int n,unsigned char *out){ return d->read(lba,n,out); },t,g,e);
    error="Audio CD track no longer exists.";
#else
    error="USB optical drives require a Nintendo Switch build.";
#endif
    return {};
}
TrackDetails discDetails(const std::string &path) {
    TrackDetails details;details.format="CDDA";details.sampleRate=44100;
    details.channels=2;details.bitDepth=16;details.bitRate=1411;details.album="Audio CD";
    std::string id;int number;if(!pathTrack(path,id,number))return details;
    char label[32];std::snprintf(label,sizeof label,"Track %02d",number);details.title=label;
    std::lock_guard<std::mutex> lock(catalogMutex);
    auto it=catalog.find(id);if(it==catalog.end())return details;
    auto &a=it->second;
    for(auto &t:a.toc.audio)if(t.number==number)details.duration=(t.end-t.start)/75.;
    if(a.meta.found) {
        details.album=a.meta.album;details.artist=a.meta.artist;
        auto title=a.meta.titles.find(number);if(title!=a.meta.titles.end()&&!title->second.empty())details.title=title->second;
        auto artist=a.meta.artists.find(number);if(artist!=a.meta.artists.end()&&!artist->second.empty())details.artist=artist->second;
    }
    return details;
}
std::vector<unsigned char> discCover(const std::string &path) {
    std::string id;int number;if(!pathTrack(path,id,number))return {};
    std::lock_guard<std::mutex> lock(catalogMutex);
    auto it=catalog.find(id);return it==catalog.end()?std::vector<unsigned char>{}:it->second.meta.cover;
}
void shutdownDisc() {
#ifdef __SWITCH__
    { std::lock_guard<std::mutex> lock(deviceMutex);drive.reset(); }
#endif
    std::lock_guard<std::mutex> lock(catalogMutex);catalog.clear();
}
} // namespace nxh
