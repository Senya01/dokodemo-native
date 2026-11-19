#pragma once

namespace dokodemo::gameplay {
class RoomVocabulary;

// B009 filters the separate G3151 friend table into CT24DF with CT2B01.
unsigned CountIntroductionFriends(const RoomVocabulary& vocabulary);
}
