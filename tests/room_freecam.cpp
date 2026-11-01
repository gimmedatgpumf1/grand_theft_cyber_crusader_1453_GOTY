#include "client/room_freecam.h"
#include <cassert>
#include <cmath>
#include <cstdio>

using namespace gtc::native;

static WindowEvent key(WindowEvent::Type type, int value, bool repeat = false) {
    WindowEvent event{type, value}; event.repeat = repeat; return event;
}
static bool near(double a, double b, double tolerance = 1e-8) { return std::abs(a - b) < tolerance; }

int main() {
    using Type = WindowEvent::Type;
    using Action = RoomFreecam::Action;
    RoomFreecam camera;
    const Camera initial = camera.camera(640, 480, true);
    const Vec3 ahead = normalized(initial.target - initial.position);
    assert(ahead.z < -0.99 && ahead.y < 0 && initial.worldSpaceGpu);
    camera.handle(key(Type::KeyDown, 'w'), true);
    camera.move(1.0);
    const Camera forward = camera.camera(640, 480, true);
    assert(length((forward.position - initial.position) - ahead * 1.8) < 1e-8);
    camera.handle(key(Type::KeyUp, 'w'), true);
    camera.handle(key(Type::KeyDown, 'd'), true);
    camera.move(1.0);
    const Camera right = camera.camera(640, 480, true);
    assert(near(right.position.x - forward.position.x, 1.8) && near(right.position.z, forward.position.z));
    camera.handle(key(Type::KeyUp, 'd'), true);
    camera.handle(key(Type::KeyDown, ' '), true);
    camera.move(1.0);
    const Camera above = camera.camera(640, 480, true);
    assert(near(above.position.y - right.position.y, 1.8));
    camera.handle(key(Type::KeyUp, ' '), true);
    camera.handle(key(Type::KeyDown, kKeyLeftControl), true);
    camera.move(1.0);
    assert(near(camera.camera(640, 480, true).position.y, right.position.y));
    camera.handle(key(Type::KeyUp, kKeyLeftControl), true);
    WindowEvent look{Type::MouseMove}; look.dx = 100; look.dy = -100;
    const Camera beforeLook = camera.camera(640, 480, true);
    camera.handle(look, false);
    assert(length(camera.camera(640, 480, true).target - beforeLook.target) < 1e-8);
    camera.handle(look, true);
    const Camera afterLook = camera.camera(640, 480, true);
    assert(afterLook.target.x > beforeLook.target.x && afterLook.target.y > beforeLook.target.y);
    camera.handle(key(Type::KeyDown, 'w'), true);
    camera.handle(key(Type::KeyDown, 'd'), true);
    camera.handle(key(Type::KeyDown, kKeyShift), true);
    const Vec3 start = camera.camera(640, 480, true).position;
    camera.move(1.0);
    assert(near(length(camera.camera(640, 480, true).position - start), 5.4));
    // No room collision or distance clamp: the camera can fly through walls.
    camera.move(10.0);
    assert(length(camera.camera(640, 480, true).position - start) > 50);
    assert(camera.fingerJoint()==0);
    camera.handle(key(Type::KeyDown,kKeyLeftControl),true); assert(camera.fingerJoint()==1);
    camera.handle(key(Type::KeyDown,kKeyAlt),true); assert(camera.fingerJoint()==3);
    camera.handle(key(Type::KeyUp,kKeyLeftControl),true); assert(camera.fingerJoint()==2);
    camera.handle(key(Type::KeyUp,kKeyAlt),true); assert(camera.fingerJoint()==0);
    for (int bend : {'1','5','6','0','q','e','f','g','h','j','k','l','z','x','n','m','u','i','o'}) {
        assert(camera.handle(key(Type::KeyDown,bend),true)==Action::BendJoints);
        assert(camera.handle(key(Type::KeyDown,bend,true),true)==Action::BendJoints);
    }
    assert(camera.handle(key(Type::KeyDown,'b'),true)==Action::RestoreOriginal);
    assert(camera.handle(key(Type::KeyDown,'b',true),true)==Action::None);
    assert(camera.handle(key(Type::KeyDown,'r'),true)==Action::RestorePose);
    assert(camera.handle(key(Type::KeyDown,'r',true),true)==Action::None);
    assert(camera.handle(key(Type::KeyDown, 'v', true), true) == Action::None);
    assert(camera.handle(key(Type::KeyDown, 'v'), true) == Action::ToggleLines);
    assert(camera.handle(key(Type::KeyDown, 'c', true), true) == Action::None);
    assert(camera.handle(key(Type::KeyDown, 'c'), true) == Action::Exit);
    assert(camera.handle(key(Type::KeyDown, kKeyF12), true) == Action::Screenshot);
    assert(camera.handle(key(Type::KeyDown, kKeyEscape), true) == Action::Release);
    const Vec3 stopped = camera.camera(640, 480, true).position;
    camera.move(1.0);
    assert(length(camera.camera(640, 480, true).position - stopped) < 1e-8);
    WindowEvent click{Type::MouseDown}; click.button = 1;
    assert(camera.handle(click, false) == Action::Capture);
    camera.handle(key(Type::KeyDown, 'w'), true);
    assert(camera.handle(WindowEvent{Type::FocusLoss}, true) == Action::Release);
    camera.move(1.0);
    assert(length(camera.camera(640, 480, true).position - stopped) < 1e-8);
    WindowEvent wheel{Type::Wheel}; wheel.wheel = 2;
    camera.handle(wheel, true);
    assert(near(length(camera.camera(640, 480, true).position - stopped), 0.5));
    std::puts("room freecam controls: PASS");
}
