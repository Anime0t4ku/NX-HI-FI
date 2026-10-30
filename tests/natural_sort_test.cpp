#include "core.hpp"
#include <cassert>
#include <iostream>
#include <random>
using namespace nxh;
static std::vector<std::string> sorted(std::initializer_list<const char*> names){std::vector<Entry> entries;for(auto name:names)entries.push_back({name,name,false});sortEntries(entries);std::vector<std::string> out;for(auto &e:entries)out.push_back(e.name);return out;}
int main(){
    assert((sorted({"1","10","2","3"})==std::vector<std::string>{"1","2","3","10"}));
    assert((sorted({"Track 10.flac","track 2.flac","Track 1.flac"})==std::vector<std::string>{"Track 1.flac","track 2.flac","Track 10.flac"}));
    assert((sorted({"Disc 10 Track 2","Disc 2 Track 10","Disc 2 Track 2"})==std::vector<std::string>{"Disc 2 Track 2","Disc 2 Track 10","Disc 10 Track 2"}));
    assert((sorted({"001","01","1","0","00"})==std::vector<std::string>{"0","00","1","01","001"}));
    std::vector<Entry> folders{{"2","two",false},{"10","folder ten",true},{"3","folder three",true},{"1","one",false}};sortEntries(folders);
    assert(folders[0].name=="3"&&folders[1].name=="10"&&folders[2].name=="1"&&folders[3].name=="2");
    auto huge=sorted({"999999999999999999999999999999999999999999999999999999999999999999999","1000000000000000000000000000000000000000000000000000000000000000000000"});assert(huge[0][0]=='9');
    std::vector<Entry> values;for(int i=1;i<=250;i++)values.push_back({std::to_string(i),std::to_string(i),false});std::mt19937 random(42);
    for(int round=0;round<20;round++){std::shuffle(values.begin(),values.end(),random);sortEntries(values);for(int i=0;i<250;i++)assert(values[i].name==std::to_string(i+1));}
    std::cout<<"Natural numeric sorting, folders first, multiple digit runs, case, leading zeroes and overflow-safe long numbers passed.\n";
}
