//
// Created by Owner on 2024/02/06.
//

#ifndef NEWDIRECTORY_CAMERA_H
#define NEWDIRECTORY_CAMERA_H


#include <cstdint>

class camera {

public:
    static void Main(int *position, const int32_t *actions, uint64_t * NowState);

private:
    static void onFreeCameraMove(int *position, int action, int param5, uint64_t * NowState);
};


#endif //NEWDIRECTORY_CAMERA_H
