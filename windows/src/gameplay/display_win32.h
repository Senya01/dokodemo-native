#pragma once

#include "display.h"
#include <windows.h>

namespace dokodemo::gameplay {

// Paint one materialized scanout into the destination DC at native size.
bool PresentDisplay(HDC dc, const Display& display);

}
