#include "gameplay/profile_date.h"
#include <iostream>
#include <stdexcept>

using namespace dokodemo::gameplay;

namespace {
void Check(bool pass, const char* why) { if (!pass) throw std::runtime_error(why); }
void Tick(ProfileDateEditor& e, unsigned count, unsigned buttons = 0) {
    for (unsigned i=0; i<count; ++i) e.Step(buttons);
}
void Press(ProfileDateEditor& e, unsigned button) { e.Step(button); e.Step(0); }
void Choose(ProfileDateEditor& e, unsigned button = 0x2000) {
    e.Step(button);
    Check(e.closing(),"selection did not start close");
    Tick(e,9);
    Check(!e.result() && e.closing(),"editor committed before close");
    e.Step(0);
    if (!e.result()) Tick(e,10);
}
}

int main() try {
    Check(ProfileMonthDays(1900,2)==28 && ProfileMonthDays(2000,2)==29 &&
          ProfileMonthDays(1984,2)==29 && ProfileMonthDays(2100,2)==28,"Gregorian leap days");
    constexpr int ordinary[]{31,28,31,30,31,30,31,31,30,31,30,31};
    constexpr int cutoff[]{20,19,21,20,21,22,23,23,23,24,22,22};
    for (int month=1; month<=12; ++month) {
        Check(ProfileMonthDays(1981,month)==ordinary[month-1],"ordinary month");
        Check(ProfileZodiac(month,cutoff[month-1]-1)==(month+8)%12 &&
              ProfileZodiac(month,cutoff[month-1])==(month+9)%12,"sign boundary");
    }
    const ProfileDate prior{1984,2,29,11};
    ProfileDateEditor e;
    e.Open(2026,{},0x2000);
    Tick(e,12,0x2000);
    Check(e.mode()==3 && e.editing() && e.base_year()==1926 && e.cursor()==54 &&
          e.first_visible()==50 && !e.result(),"first-run year cursor and held input");
    Press(e,0x4000);
    Check(e.editing(),"first-run year cannot cancel without complete form");
    Choose(e);
    Check(e.mode()==4 && e.visible_date().year==1980 && e.cursor()==0,"source year return");
    Press(e,0x10); Check(e.cursor()==11,"month wraps up on fresh press");
    Choose(e,0x4000);
    Check(e.mode()==3 && e.cursor()==54 && e.visible_date().year==-1,"month cancel returns to backup year");
    Choose(e);
    Press(e,0x40); Choose(e);
    Check(e.mode()==5 && e.count()==29 && e.visible_date().month==2,"leap day selector");
    Press(e,0x10); Check(e.cursor()==28,"day grid wraps row up");
    Press(e,0x20); Check(e.cursor()==0,"last cell wraps to first");
    Choose(e,0x4000);
    Check(e.mode()==4 && e.cursor()==0 && e.visible_date().year==1980,"day cancel restores backup month selection");
    Choose(e);
    Press(e,0x80); Check(e.cursor()==30,"day grid wraps left");
    Choose(e);
    Check(e.result() && e.result()->year==1980 && e.result()->month==1 &&
          e.result()->day==31 && e.result()->zodiac==10 && !e.cancelled(),"date accepted");
    Check(ProfileDateText(*e.result())=="1980\x94\x4e 1\x8c\x8e" "31\x93\xfa","display padding");
    Check(ProfileZodiacText(9)=="\x82\xe2\x82\xac\x8d\xc0","Capricorn CP932");
    e.Open(2026,prior,0);
    Tick(e,10);
    Press(e,0x40); Choose(e);
    Choose(e);
    Check(e.count()==28 && e.cursor()==27,"saved leap day clamps to last legal day in changed year");
    Choose(e,0x4000); Choose(e,0x4000); Choose(e,0x4000);
    Check(e.cancelled() && e.result()->year==1984 && e.result()->month==2 &&
          e.result()->day==29 && e.result()->zodiac==11,"final cancel restores all four fields");
    e.Open(2026,{2026,1,1,9},0);
    Tick(e,10);
    Press(e,0x10); // 2025
    Tick(e,17,0x40);
    Check(e.cursor()==100,"held Down stops at current year");
    e.Step(0); Press(e,0x40);
    Check(e.cursor()==0,"new Down wraps to year base");
    std::cout<<"Date SELECT year/month/day, calendar, zodiac, rollback and display passed\n";
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
