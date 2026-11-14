#include "common.h"
#include "client/playground_pair.h"
#include "client/playground_shapes.h"
#define private public
#include "client/playground_room.h"
#undef private
#include <cstdio>
int main(){
    PlaygroundRoom room; room.reset();
    for (int i=0;i<40;++i) room.advance(0.05);
    auto& s = room.bodies_[PlaygroundRoom::kSeesaw];
    s.angle = 0.40; PlaygroundRoom::placeCorners(s); s.previous=s.position; s.previousCentre=s.centre;
    s.previousAngle=s.angle; s.velocity={}; s.spin=0.0; s.frozen=false;
    room.ball_.position = {-3.207, 1.383, 0}; room.ball_.velocity={}; room.ball_.frozen=false;
    room.ball_.previous=room.ball_.position;
    for (int k=0;k<6;++k){ room.advance(0.05);
      std::printf("t%d plank frozen %d a%+.4f sp%+.3f restTicks %d | ball frozen %d y %.4f vy %+.3f\n", k,
        int(s.frozen), s.angle, s.spin, int(s.restTicks), int(room.ball_.frozen), room.ball_.position.y, room.ball_.velocity.y);
      std::printf("    after: ball frozen %d touching %d standing %d\n", int(room.ball_.frozen), int(room.ball_.touching), int(room.ball_.standing));
      room.ball_.frozen=false;
    }
    return 0;
}
