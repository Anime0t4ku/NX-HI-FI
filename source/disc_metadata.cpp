#include "disc.hpp"
#include <chrono>
#include <ctime>
#include <thread>
#include <cstring>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
namespace nxh {
namespace {
uint32_t rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
std::string sha1ID(std::string s) {
    uint64_t bits = s.size() * 8;
    s += char(128);
    while (s.size() % 64 != 56) s += char(0);
    for (int i = 7; i >= 0; --i) s += char(bits >> (8 * i));
    uint32_t h[] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    for (size_t off = 0; off < s.size(); off += 64) {
        uint32_t w[80]{};
        for (int i = 0; i < 16; ++i)
            for (int j = 0; j < 4; ++j) w[i] = (w[i] << 8) | uint8_t(s[off+i*4+j]);
        for (int i = 16; i < 80; ++i) w[i] = rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h[0],b=h[1],c=h[2],d=h[3],e=h[4];
        for (int i=0;i<80;++i) {
            uint32_t f,k;
            if(i<20){f=(b&c)|(~b&d);k=0x5a827999;}
            else if(i<40){f=b^c^d;k=0x6ed9eba1;}
            else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdc;}
            else {f=b^c^d;k=0xca62c1d6;}
            auto t=rol(a,5)+f+e+k+w[i];e=d;d=c;c=rol(b,30);b=a;a=t;
        }
        h[0]+=a;h[1]+=b;h[2]+=c;h[3]+=d;h[4]+=e;
    }
    unsigned char bytes[20];
    for(int i=0;i<20;++i)bytes[i]=h[i/4]>>(24-8*(i%4));
    const char *alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789._";
    std::string out;
    for(int i=0;i<20;i+=3){
        unsigned v=unsigned(bytes[i])<<16;
        if(i+1<20)v|=unsigned(bytes[i+1])<<8;
        if(i+2<20)v|=bytes[i+2];
        out+=alphabet[(v>>18)&63];out+=alphabet[(v>>12)&63];
        out+=i+1<20?alphabet[(v>>6)&63]:'-';out+=i+2<20?alphabet[v&63]:'-';
    }
    return out;
}
std::string credit(const nlohmann::json &j) {
    std::string out;
    if (!j.is_array()) return out;
    for (auto &c : j) {
        auto name = c.value("name", std::string{});
        if (name.empty() && c.contains("artist")) name=c["artist"].value("name",std::string{});
        out+=name+c.value("joinphrase",std::string{});
    }
    while (!out.empty() && out.back()==' ') out.pop_back();
    return out;
}
nlohmann::json encode(const DiscMetadata &m) {
    nlohmann::json titles=nlohmann::json::object(),artists=nlohmann::json::object();
    for(auto &t:m.titles)titles[std::to_string(t.first)]=t.second;
    for(auto &a:m.artists)artists[std::to_string(a.first)]=a.second;
    return {{"found",m.found},{"at",m.at},{"album",m.album},{"artist",m.artist},
            {"release",m.release},{"group",m.group},{"titles",titles},{"artists",artists}};
}
bool uuid(const std::string &s) {
    if(s.size()!=36)return false;
    for(size_t i=0;i<s.size();++i)
        if(i==8||i==13||i==18||i==23){if(s[i]!='-')return false;}
        else if(!std::isxdigit(static_cast<unsigned char>(s[i])))return false;
    return true;
}
std::string escape(const std::string &s) {
    const char *hex="0123456789ABCDEF";std::string out;
    for(unsigned char c:s) {
        if(std::isalnum(c)||c=='-'||c=='_'||c=='.'||c=='~')out+=c;
        else {out+='%';out+=hex[c>>4];out+=hex[c&15];}
    }
    return out;
}
std::string normalized(std::string s) {
    std::string out;
    for(unsigned char c:s)if(std::isalnum(c)||c>=128)out+=char(c<128?std::tolower(c):c);
    return out;
}
std::vector<unsigned char> readBytes(const std::string &p,size_t max) {
    std::ifstream f(p,std::ios::binary|std::ios::ate);
    auto n=f.tellg();if(!f||n<0||uint64_t(n)>max)return {};
    std::vector<unsigned char>b(static_cast<size_t>(n));
    f.seekg(0);if(n&&!f.read(reinterpret_cast<char*>(b.data()),n))return {};return b;
}
void saveBytes(const std::string &p,const unsigned char *b,size_t n) {
    std::ofstream f(p+".tmp",std::ios::binary);f.write(reinterpret_cast<const char*>(b),n);f.close();
    if(f)std::rename((p+".tmp").c_str(),p.c_str());else std::remove((p+".tmp").c_str());
}
} // namespace
std::string discID(const DiscTOC &t) {
    char h[16];std::string text;
    std::snprintf(h,sizeof h,"%02X%02X",t.first,t.last);text=h;
    for(auto o:t.offsets){std::snprintf(h,sizeof h,"%08X",o);text+=h;}
    return sha1ID(text);
}
bool parseDiscTOC(const unsigned char *b,size_t n,DiscTOC &out,std::string &error) {
    out={};
    auto fail=[&]{error="Invalid or incomplete audio CD table of contents.";return false;};
    if(!b||n<4)return fail();
    size_t length=(size_t(b[0])<<8|b[1])+2;
    if(length>n||length<20||(length-4)%8)return fail();
    DiscTOC t;t.first=b[2];t.last=b[3];
    if(t.first<1||t.last>99||t.first>t.last)return fail();
    std::map<int,std::pair<int,bool>> entries;
    int lead=-1;
    for(size_t p=4;p+8<=length;p+=8) {
        int number=b[p+2],min=b[p+5],sec=b[p+6],frame=b[p+7];
        if(sec>=60||frame>=75)return fail();
        int offset=(min*60+sec)*75+frame;
        if(number==0xaa){if(lead>=0)return fail();lead=offset;}
        else {
            if(number<t.first||number>t.last||entries.count(number))return fail();
            entries[number]={offset,(b[p+1]&4)==0};
        }
    }
    if(lead<150||int(entries.size())!=t.last-t.first+1)return fail();
    t.offsets[0]=lead;
    for(int i=t.first;i<=t.last;++i) {
        int start=entries.at(i).first,end=i==t.last?lead:entries.at(i+1).first;
        if(start<150||start>=end)return fail();
        t.offsets[i]=start;
        if(entries.at(i).second)t.audio.push_back({i,start-150,end-150});
    }
    if(t.audio.empty()){error="Disc contains no audio tracks.";return false;}
    out=std::move(t);return true;
}
DiscMetadata parseDiscMetadata(const nlohmann::json &j,const std::string &id,int64_t now) {
    DiscMetadata info;info.at=now;
    for(auto &r:j.value("releases",nlohmann::json::array()))
        for(auto &m:r.value("media",nlohmann::json::array())) {
            bool match=false;
            for(auto &d:m.value("discs",nlohmann::json::array()))if(d.value("id",std::string{})==id)match=true;
            auto tracks=m.value("tracks",nlohmann::json::array());
            if(!match||tracks.empty())continue;
            info.found=true;info.album=r.value("title",std::string{});
            info.artist=credit(r.value("artist-credit",nlohmann::json::array()));
            info.release=r.value("id",std::string{});
            if(r.contains("release-group"))info.group=r["release-group"].value("id",std::string{});
            for(auto &t:tracks) {
                int pos=t.value("position",0);if(pos<1||pos>99)continue;
                info.titles[pos]=t.value("title",std::string{});
                info.artists[pos]=credit(t.value("artist-credit",nlohmann::json::array()));
            }
            return info;
        }
    return info;
}
DiscMetadata lookupDiscMetadata(const std::string &id,const std::string &dir,const DiscHTTP &http,const DiscArtCheck &validCover) {
    // Single lookup at a time, including cache writes. MusicBrainz permits one request per second.
    static std::mutex mutex;
    static auto last=std::chrono::steady_clock::time_point{};
    std::lock_guard<std::mutex> lock(mutex);
    int64_t now=std::time(nullptr);
    auto path=dir+"/"+id;
    try {
        auto bytes=readBytes(path+".json",4<<20);
        auto j=nlohmann::json::parse(bytes.begin(),bytes.end(),nullptr,false);
        if(j.is_object()) {
            DiscMetadata m;m.found=j.at("found");m.at=j.at("at");
            if(m.found||(now>=m.at&&now-m.at<7*86400)) {
                m.album=j.value("album",std::string{});m.artist=j.value("artist",std::string{});
                m.release=j.value("release",std::string{});m.group=j.value("group",std::string{});
                for(auto &v:j.at("titles").items())m.titles[std::stoi(v.key())]=v.value();
                for(auto &v:j.at("artists").items())m.artists[std::stoi(v.key())]=v.value();
                m.cover=readBytes(path+".jpg",8<<20);return m;
            }
        }
    } catch(...) {} // A broken cache must never prevent disc playback.
    auto elapsed=std::chrono::steady_clock::now()-last;
    if(elapsed<std::chrono::seconds(1))std::this_thread::sleep_for(std::chrono::seconds(1)-elapsed);
    last=std::chrono::steady_clock::now();
    std::vector<unsigned char> bytes;
    int status=http("https://musicbrainz.org/ws/2/discid/"+id+"?inc=artists%2Brecordings%2Brelease-groups&fmt=json",bytes,4<<20);
    DiscMetadata m;m.at=now;
    if(status==200) {
        try {
            auto j=nlohmann::json::parse(bytes.begin(),bytes.end());
            if(!j.is_object()||!j.contains("releases")||!j["releases"].is_array())return {};
            m=parseDiscMetadata(j,id,now);
        }catch(...){return {};}
    } else if(status!=404)return {}; // Network failures and busy servers are never negative-cached.
    if(m.found) {
        auto art=[&](const std::string &url) {
            std::vector<unsigned char>b;
            if(http(url,b,8<<20)==200&&b.size()>24&&
               ((b[0]==255&&b[1]==216)||!std::memcmp(b.data(),"\x89PNG\r\n\x1a\n",8))&&
               (!validCover||validCover(b))) {
                m.cover=std::move(b);return true;
            }
            return false;
        };
        if((!uuid(m.release)||!art("https://coverartarchive.org/release/"+m.release+"/front-500"))&&
           (!uuid(m.group)||!art("https://coverartarchive.org/release-group/"+m.group+"/front-500"))&&
           !m.artist.empty()&&!m.album.empty()) {
            bytes.clear();
            if(http("https://itunes.apple.com/search?term="+escape(m.artist+" "+m.album)+"&media=music&entity=album&limit=10",bytes,1<<20)==200) {
                try {
                    auto j=nlohmann::json::parse(bytes.begin(),bytes.end());
                    for(auto &r:j.value("results",nlohmann::json::array()))
                        if(normalized(r.value("artistName",std::string{}))==normalized(m.artist)&&
                           normalized(r.value("collectionName",std::string{}))==normalized(m.album)) {
                            auto url=r.value("artworkUrl100",std::string{});
                            if(url.rfind("https://",0)!=0)continue;
                            auto at=url.find("100x100bb");if(at!=std::string::npos)url.replace(at,9,"600x600bb");
                            if(art(url))break;
                        }
                }catch(...){}
            }
        }
    }
    mkdir(dir.c_str(),0777);
    if(!m.cover.empty())saveBytes(path+".jpg",m.cover.data(),m.cover.size());
    auto data=encode(m).dump();saveBytes(path+".json",reinterpret_cast<const unsigned char*>(data.data()),data.size());
    return m;
}
} // namespace nxh
