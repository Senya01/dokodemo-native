#include "gameplay/room_word_reading.h"
#include <iostream>
#include <stdexcept>

using dokodemo::gameplay::FinishWordReading;
namespace {
void Check(bool value) { if(!value) throw std::runtime_error("Wrong source reading or registration"); }
}
int main() {
    try {
        const std::string kanji="\x96\xbc", na="\x82\xc8", katakana="\x83\x69";
        int calls=0;
        std::string key,form,reading="old";
        auto record=[&](std::string_view k,std::string_view v) {
            ++calls; key=k; form=v; return true;
        };
        Check(FinishWordReading("",reading,record) && reading=="old" && calls==0);
        Check(FinishWordReading(katakana,reading,record) && reading==na && calls==0);
        reading=na;
        Check(FinishWordReading(kanji,reading,record) && reading==na && calls==1 && key==na && form==kanji);
        reading=kanji+katakana;
        Check(FinishWordReading(kanji,reading,record) && reading==na && key==kanji+katakana);
        reading.clear();
        Check(FinishWordReading(kanji,reading,record) && reading.empty() && key==kanji && form==kanji);
        reading=katakana;
        Check(!FinishWordReading(kanji,reading,{}) && reading==katakana);
        std::cout<<"Word reading normalization and conditional NJ arguments passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 1; }
}
