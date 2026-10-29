// rmtc3/types.cpp
#include "rmtc3/types.hpp"

namespace rmtc3 {

const int kDirX[4] = {0, 1, 0, -1};
const int kDirY[4] = {-1, 0, 1, 0};
const char* const kDirName[4] = {"up", "right", "down", "left"};

bool dir_action(int index, bool attack, Action* out) {
    if (!out || index < 0 || index > 3) return false;
    *out = attack ? Action::attack(kDirX[index], kDirY[index])
                  : Action::move(kDirX[index], kDirY[index]);
    return true;
}

}  // namespace rmtc3
