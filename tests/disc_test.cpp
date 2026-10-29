#include "disc.hpp"
#include "disc_input.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>
using namespace nxh;
int main() {
    DiscTOC t;t.first=1;t.last=15;t.offsets[0]=258725;
    int offsets[]={150,17510,33275,45910,57805,78310,94650,109580,132010,149160,165115,177710,203325,215555,235590};
    for(int i=0;i<15;++i)t.offsets[i+1]=offsets[i];
    assert(discID(t)=="TqvKjMu7dMliSfmVEBtrL7sBSno-");
    std::vector<unsigned char> toc(4+8*4);toc[1]=toc.size()-2;toc[2]=1;toc[3]=3;
    for(int i=0;i<4;++i) {
        int frame=150+i*750;size_t p=4+8*i;
        toc[p+1]=i==0?0x14:0x10;toc[p+2]=i<3?i+1:0xaa;
        toc[p+5]=frame/(60*75);toc[p+6]=frame/75%60;toc[p+7]=frame%75;
    }
    std::string error;DiscTOC parsed;
    assert(parseDiscTOC(toc.data(),toc.size(),parsed,error));
    assert(parsed.audio.size()==2&&parsed.audio[0].number==2);
    assert(parsed.audio[0].start==750&&parsed.audio[1].end==2250);
    assert(!parseDiscTOC(toc.data(),toc.size()-2,parsed,error));
    auto broken=toc;broken[3]=100;
    assert(!parseDiscTOC(broken.data(),broken.size(),parsed,error));
    broken=toc;broken[4+8*3+7]=75;
    assert(!parseDiscTOC(broken.data(),broken.size(),parsed,error));
    // Raw CD reads span sectors and stop at the exclusive track end; the WAV is virtual.
    int reads=0;
    auto read=[&](int lba,int count,unsigned char*out){
        ++reads;assert(lba>=10&&lba+count<=30);
        for(int i=0;i<count*2352;++i)out[i]=(lba*2352+i)%251;
        return true;
    };
    CDDAInput input(read,{1,10,30},nullptr,0);
    assert(input.size()==44+20*2352);
    std::vector<unsigned char> bytes(input.size());
    size_t got=0;
    while(got<bytes.size()) {auto n=input.read(bytes.data()+got,std::min<size_t>(777,bytes.size()-got));assert(n);got+=n;}
    assert(!memcmp(bytes.data(),"RIFF",4)&&!memcmp(bytes.data()+8,"WAVE",4));
    assert(bytes[24]==0x44&&bytes[25]==0xac&&bytes[34]==16);
    for(size_t i=44;i<bytes.size();++i)assert(bytes[i]==(10*2352+i-44)%251);
    assert(reads==2&&input.read(bytes.data(),4)==0);
    assert(!input.seek(-1,SEEK_SET)&&!input.seek(1,SEEK_END));
    assert(input.seek(44+2352*3+17,SEEK_SET));
    assert(input.read(bytes.data(),8)==8&&bytes[0]==(13*2352+17)%251);
    CDDAInput failing([](int,int,unsigned char*){return false;},{1,0,20},nullptr,0);
    failing.seek(44,SEEK_SET);assert(!failing.read(bytes.data(),4)&&!failing.error().empty());
    std::atomic<uint64_t> generation{1};
    CDDAInput cancelled(read,{1,10,30},&generation,0);
    assert(cancelled.read(bytes.data(),100)==0);
    // Match the correct medium, preserve physical track positions and joined credits.
    nlohmann::json response={{"releases",nlohmann::json::array({
        {{"id","wrong"},{"media",nlohmann::json::array({{{"discs",nlohmann::json::array({{{"id","other"}}})}}})}},
        {{"id","12345678-1234-1234-1234-123456789012"},{"title","Album"},
         {"artist-credit",nlohmann::json::array({{{"name","A"},{"joinphrase"," & "}},{{"artist",{{"name","B"}}}}})},
         {"media",nlohmann::json::array({{{"discs",nlohmann::json::array({{{"id",discID(t)}}})},
          {"tracks",nlohmann::json::array({{{"position",2},{"title","Song two"},{"artist-credit",nlohmann::json::array({{{"name","Solo"}}})}}})}}})}}
    })}};
    auto m=parseDiscMetadata(response,discID(t),123);
    assert(m.found&&m.album=="Album"&&m.artist=="A & B");
    assert(m.titles[2]=="Song two"&&m.artists[2]=="Solo"&&m.at==123);
    char temp[]="/tmp/nxh-disc-XXXXXX";auto dir=mkdtemp(temp);assert(dir);
    int calls=0;
    auto http=[&](const std::string&url,std::vector<unsigned char>&b,size_t){
        ++calls;
        if(url.find("musicbrainz.org")!=std::string::npos){auto text=response.dump();b.assign(text.begin(),text.end());return 200;}
        return 404;
    };
    m=lookupDiscMetadata(discID(t),dir,http);assert(m.found);int before=calls;
    m=lookupDiscMetadata(discID(t),dir,http);assert(m.found&&calls==before);
    std::string negative=std::string(dir)+"/negative";std::filesystem::create_directory(negative);
    calls=0;
    auto missing=[&](const std::string&,std::vector<unsigned char>&,size_t){++calls;return 404;};
    assert(!lookupDiscMetadata(discID(t),negative,missing).found);
    assert(!lookupDiscMetadata(discID(t),negative,missing).found&&calls==1);
    {std::ifstream f(negative+"/"+discID(t)+".json");nlohmann::json j;f>>j;j["at"]=1;std::ofstream o(negative+"/"+discID(t)+".json");o<<j;}
    lookupDiscMetadata(discID(t),negative,missing);assert(calls==2);
    std::string offline=std::string(dir)+"/offline";std::filesystem::create_directory(offline);calls=0;
    auto unavailable=[&](const std::string&,std::vector<unsigned char>&,size_t){++calls;return 503;};
    lookupDiscMetadata(discID(t),offline,unavailable);lookupDiscMetadata(discID(t),offline,unavailable);
    assert(calls==2&&!std::filesystem::exists(offline+"/"+discID(t)+".json"));
    std::string artwork=std::string(dir)+"/art";std::filesystem::create_directory(artwork);
    std::vector<std::string> urls;
    auto artHTTP=[&](const std::string &url,std::vector<unsigned char>&b,size_t) {
        urls.push_back(url);
        if(url.find("musicbrainz.org")!=std::string::npos) {
            auto text=response.dump();b.assign(text.begin(),text.end());return 200;
        }
        if(url.find("coverartarchive.org")!=std::string::npos) {
            // A corrupt cover with JPEG magic must fall through when the decoder rejects it.
            b.assign(30,0);b[0]=255;b[1]=216;return 200;
        }
        if(url.find("itunes.apple.com/search")!=std::string::npos) {
            auto text=nlohmann::json{{"results",nlohmann::json::array({
                {{"artistName","A & B"},{"collectionName","Wrong Album"},{"artworkUrl100","https://art.example/wrong"}},
                {{"artistName","A & B"},{"collectionName","Album"},{"artworkUrl100","https://art.example/100x100bb.jpg"}}
            })}}.dump();b.assign(text.begin(),text.end());return 200;
        }
        b.assign(30,1);b[0]=255;b[1]=216;return 200;
    };
    m=lookupDiscMetadata(discID(t),artwork,artHTTP,[](const auto&b){return b[2]==1;});
    assert(m.found&&m.cover.size()==30&&m.cover[2]==1);
    assert(urls.back()=="https://art.example/600x600bb.jpg");
    std::filesystem::remove_all(dir);
    std::cout<<"Disc TOC, MusicBrainz/cache and CDDA input tests passed\n";
}
