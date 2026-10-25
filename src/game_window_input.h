#pragma once
#include <cstdint>

namespace native_port {
inline uint32_t GameWindowControllerButtons(uint32_t physical,uint32_t window,bool focused,bool editor_live) {
    return focused&&!editor_live?(physical|window):0;
}
// Window-event button state only. The runtime still owns input publication.
class GameWindowButtons {
public:
    uint32_t Mask() const { return mask_; }
    void FocusLost() { mask_=0; }
    bool Update(uint32_t bit,bool down,bool repeat,bool focused,bool editor_live) {
        const auto before=mask_;
        if(!focused)mask_=0;
        else if(bit&&!(down&&editor_live)&&!(down&&repeat&&!(mask_&bit)))
            mask_=down?(mask_|bit):(mask_&~bit);
        return before!=mask_;
    }
private:
    uint32_t mask_=0;
};
}
