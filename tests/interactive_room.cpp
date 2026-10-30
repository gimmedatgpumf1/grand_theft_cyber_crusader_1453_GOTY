// Load dependencies before exposing the translation-unit-local App test seam.
#include "client/client.h"
#include "client/interactive.h"
#include "client/renderer.h"
#include "client/skeleton_room.h"
#include "client/room_freecam.h"
#include "client/view_controls.h"
#include "client/diagnostics.h"
#include "client/cheat_menu.h"
#include "client/crosshair.h"
#include "client/fps_counter.h"
#include "client/mode_bar.h"
#include "client/interaction_wheel.h"
#include "client/overlay_panel.h"
#include "client/status_panel.h"
#include "client/target_prompt.h"
#include "client/ui_layer.h"
#include "client/window.h"
#include "core/projectile.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <set>
#include <utility>
#include <cassert>
#include <arpa/inet.h>
#include <future>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include "net/sha1.h"
#include "net/websocket.h"
#include "core/json.h"
#define private public
#include "../native/client/interactive.cpp"
#undef private

using namespace gtc::native;
static WindowEvent down(int k) { return {WindowEvent::Type::KeyDown,k}; }
static gtc::JsonValue receive(int fd) {
    std::string data; int op = 0;
    assert(net::receiveWebSocket(fd, data, op));
    bool valid = false; auto message = gtc::JsonValue::parse(data, valid);
    assert(valid && op == 1); return message;
}
static bool neutral(const gtc::JsonValue& msg) {
    return std::abs(msg.numberAt("x")) < 0.0001 && std::abs(msg.numberAt("z")) < 0.0001 &&
       !msg.boolAt("jump") && !msg.boolAt("grab") && !msg.boolAt("piss") && !msg.boolAt("block") &&
       !msg.boolAt("sprint") && !msg.boolAt("slow") && !msg.boolAt("crouch") && !msg.boolAt("aim");
}
int main() {
    // A real loopback WebSocket captures the actual input messages, rather than
    // assuming that a key handler or a fake renderer implies correct gameplay.
    int listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    assert(listen(listener, 1) == 0);
    socklen_t size = sizeof(address);
    assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size) == 0);
    std::promise<int> accepted;
    std::thread server([&] {
        int fd = accept(listener, nullptr, nullptr); assert(fd >= 0);
        std::string request; char c;
        while (request.find("\r\n\r\n") == std::string::npos) {
            assert(recv(fd, &c, 1, 0) == 1); request += c;
        }
        const std::string prefix = "Sec-WebSocket-Key: ";
        const auto start = request.find(prefix); assert(start != std::string::npos);
        const auto end = request.find("\r\n", start);
        const std::string key = request.substr(start + prefix.size(), end - start - prefix.size());
        const std::string response = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\n"
            "Connection: Upgrade\r\nSec-WebSocket-Accept: " + net::webSocketAccept(key) + "\r\n\r\n";
        assert(net::sendAll(fd, response));
        assert(net::sendWebSocket(fd, "{\"type\":\"welcome\",\"id\":1}"));
        accepted.set_value(fd);
    });
    gtc::native::App app(InteractiveOptions{});
    std::string error;
    assert(app.client_.connect("127.0.0.1", ntohs(address.sin_port), error));
    int peer = accepted.get_future().get(); server.join(); close(listener);
    assert(app.room_.load(".",error));
    app.view_.mode = ViewMode::FirstPerson; app.view_.yaw = 0.9f; app.view_.pitch = -0.2f;
    app.handle(down('w')); app.sendInput();
    assert(!neutral(receive(peer))); // gameplay walking before C
    const auto originalView = app.view_;
    app.handle(down('c'));
    assert(app.testRoom_ && app.captured_ && neutral(receive(peer)));
    assert(!app.room_.lineView());
    std::size_t shoulder=0;
    for (;shoulder<app.room_.male().sourceLinks().size();++shoulder) {
        const auto& edge=app.room_.male().sourceLinks()[shoulder];
        if (edge.mobility=="shoulder_ball_socket" &&
            app.room_.male().sourceStructures()[edge.second].name=="Humerus_L") break;
    }
    assert(shoulder<app.room_.male().sourceLinks().size());
    const double maleBefore=*app.room_.male().referenceBend(shoulder);
    const double femaleBefore=*app.room_.female().referenceBend(shoulder);
    app.handle(down('q'));
    assert(std::abs(*app.room_.male().referenceBend(shoulder)-maleBefore-5)<1e-7);
    assert(std::abs(*app.room_.female().referenceBend(shoulder)-femaleBefore-5)<1e-7);
    WindowEvent repeatQ=down('q'); repeatQ.repeat=true; app.handle(repeatQ);
    assert(std::abs(*app.room_.male().referenceBend(shoulder)-maleBefore-10)<1e-7);
    app.handle(down(kKeyShift)); app.handle(down('q'));
    assert(std::abs(*app.room_.male().referenceBend(shoulder)-maleBefore-5)<1e-7);
    app.handle(WindowEvent{WindowEvent::Type::KeyUp,kKeyShift});
    app.handle(down('r'));
    assert(std::abs(*app.room_.male().referenceBend(shoulder)-maleBefore)<1e-7);
    assert(std::abs(*app.room_.female().referenceBend(shoulder)-femaleBefore)<1e-7);
    app.handle(down('b')); // original CT pose remains directly available
    assert(std::abs(*app.room_.male().referenceBend(shoulder)-maleBefore)>1);
    app.handle(down('r'));
    assert(std::abs(*app.room_.male().referenceBend(shoulder)-maleBefore)<1e-7);
    std::size_t hip=0;
    for (;hip<app.room_.male().sourceLinks().size();++hip) {
        const auto& edge=app.room_.male().sourceLinks()[hip];
        if (edge.mobility=="hip_ball_socket" && app.room_.male().sourceStructures()[edge.second].name=="Femur_L") break;
    }
    assert(hip<app.room_.male().sourceLinks().size());
    const double rotation=*app.room_.male().referenceHipRotation(hip);
    app.handle(down('n'));
    assert(std::abs(*app.room_.male().referenceHipRotation(hip)-rotation-5)<1e-7);
    app.handle(down('r'));
    assert(std::abs(*app.room_.male().referenceHipRotation(hip)-rotation)<1e-7);
    const double abduction=*app.room_.male().referenceHipAbduction(hip);
    app.handle(down('u'));
    assert(std::abs(*app.room_.male().referenceHipAbduction(hip)-abduction-5)<1e-7);
    app.handle(down(kKeyShift)); app.handle(down('u')); app.handle(WindowEvent{WindowEvent::Type::KeyUp,kKeyShift});
    assert(std::abs(*app.room_.male().referenceHipAbduction(hip)-abduction)<1e-7);
    std::size_t elbow=0;
    for (;elbow<app.room_.male().sourceLinks().size();++elbow) {
        const auto& edge=app.room_.male().sourceLinks()[elbow];
        if (edge.mobility=="elbow_hinge" && app.room_.male().sourceStructures()[edge.second].name=="Radius_R") break;
    }
    assert(elbow<app.room_.male().sourceLinks().size());
    const double forearm=*app.room_.male().referenceForearmRotation(elbow);
    app.handle(down('m'));
    assert(std::abs(*app.room_.male().referenceForearmRotation(elbow)-forearm-5)<1e-7);
    app.handle(down('r'));
    assert(std::abs(*app.room_.male().referenceForearmRotation(elbow)-forearm)<1e-7);
    // Momentary modifiers independently drive MCP, PIP and DIP on both
    // actual models; releasing the modifiers leaves no selected joint mode.
    for (int stage=1;stage<=3;++stage) {
        app.handle(down('r'));
        if (stage==1 || stage==3) app.handle(down(kKeyLeftControl));
        if (stage==2 || stage==3) app.handle(down(kKeyAlt));
        app.handle(down('2'));
        for (auto* body : {&app.room_.male(),&app.room_.female()}) for (char side : {'L','R'}) {
            const auto tag=std::string("_")+side;
            const std::string children[]={"HandProxP2"+tag,"HandMidP2"+tag,"HandDistP2"+tag};
            for (int joint=0;joint<3;++joint) {
                bool found=false;
                for (std::size_t i=0;i<body->sourceLinks().size();++i) {
                    const auto& edge=body->sourceLinks()[i];
                    if (body->sourceStructures()[edge.second].name==children[joint] && body->referenceBend(i)) {
                        assert(std::abs(*body->referenceBend(i)-(joint==stage-1 ? 5.0 : 0.0))<1e-7);
                        found=true; break;
                    }
                }
                assert(found);
            }
        }
        if (stage==1 || stage==3) app.handle(WindowEvent{WindowEvent::Type::KeyUp,kKeyLeftControl});
        if (stage==2 || stage==3) app.handle(WindowEvent{WindowEvent::Type::KeyUp,kKeyAlt});
    }
    // The same momentary modifiers operate on MTP, PIP and DIP of each toe.
    for (int stage=1;stage<=3;++stage) {
        app.handle(down('r'));
        std::map<std::pair<SkeletonModel*,std::string>,double> original;
        for (auto* body : {&app.room_.male(),&app.room_.female()}) for (const auto& edge:body->sourceLinks()) {
            const std::string& child=body->sourceStructures()[edge.second].name;
            if (child.rfind("FootProxP2_",0)!=0 && child.rfind("FootMidP2_",0)!=0 && child.rfind("FootDistP2_",0)!=0) continue;
            const auto index=static_cast<std::size_t>(&edge-body->sourceLinks().data());
            if (body->referenceBend(index)) original[{body,child}]=*body->referenceBend(index);
        }
        if (stage==1 || stage==3) app.handle(down(kKeyLeftControl));
        if (stage==2 || stage==3) app.handle(down(kKeyAlt));
        app.handle(down('7'));
        for (auto* body : {&app.room_.male(),&app.room_.female()}) for (char side : {'L','R'}) {
            const auto tag=std::string("_")+side;
            const std::string children[]={"FootProxP2"+tag,"FootMidP2"+tag,"FootDistP2"+tag};
            for (int joint=0;joint<3;++joint) {
                bool found=false;
                for (std::size_t i=0;i<body->sourceLinks().size();++i) {
                    const auto& edge=body->sourceLinks()[i];
                    if (body->sourceStructures()[edge.second].name==children[joint] && body->referenceBend(i)) {
                        const auto old=original.find({body,children[joint]}); assert(old!=original.end());
                        assert(std::abs(*body->referenceBend(i)-old->second-(joint==stage-1 ? 5.0 : 0.0))<1e-7);
                        found=true; break;
                    }
                }
                assert(found);
            }
        }
        if (stage==1 || stage==3) app.handle(WindowEvent{WindowEvent::Type::KeyUp,kKeyLeftControl});
        if (stage==2 || stage==3) app.handle(WindowEvent{WindowEvent::Type::KeyUp,kKeyAlt});
    }
    app.handle(down('r'));
    assert(!net::waitReadable(peer,0)); // room joint input never reaches the game player
    app.handle(down('v')); assert(app.room_.lineView());
    WindowEvent repeatV = down('v'); repeatV.repeat = true; app.handle(repeatV);
    assert(app.room_.lineView());
    app.handle(down('v')); assert(!app.room_.lineView());
    app.handle(down('w')); app.handle(down(' ')); app.handle(down(kKeyShift));
    app.handle(down('t')); app.handle(down('e')); app.handle(down('f'));
    WindowEvent mouse{WindowEvent::Type::MouseDown}; mouse.button = 3; app.handle(mouse);
    assert(!net::waitReadable(peer, 0)); // room keys and clicks do not send game actions
    WindowEvent motion{WindowEvent::Type::MouseMove}; motion.dx = 120; motion.dy = -40; app.handle(motion);
    const auto before = app.roomCamera_.camera(640,480,true).position;
    app.roomCamera_.move(1.0);
    assert(length(app.roomCamera_.camera(640,480,true).position-before)>1.7);
    app.sendInput(); assert(neutral(receive(peer)));
    assert(app.view_.yaw == originalView.yaw && app.view_.pitch == originalView.pitch && app.keys_.empty());
    app.handle(down('c'));
    assert(!app.testRoom_ && !app.captured_ && neutral(receive(peer)));
    assert(app.view_.yaw == originalView.yaw && app.view_.pitch == originalView.pitch);
    app.handle(down('w')); app.sendInput(); assert(!neutral(receive(peer)));
    app.captured_ = true;
    app.handle(down('c')); assert(app.testRoom_ && neutral(receive(peer)));
    app.handle(down(kKeyEscape)); assert(!app.captured_);
    mouse.button = 1; app.handle(mouse); assert(app.captured_);
    app.handle(down(kKeyF12)); assert(app.screenshot_); app.screenshot_ = false;
    app.handle(down('c')); assert(!app.testRoom_ && app.captured_ && neutral(receive(peer)));
    app.handle(down('c')); assert(app.testRoom_ && neutral(receive(peer)));
    app.handle(WindowEvent{WindowEvent::Type::FocusLoss}); assert(!app.captured_);
    app.handle(down('c')); assert(!app.testRoom_ && !app.captured_ && neutral(receive(peer)));
    close(peer);
    std::puts("interactive room input isolation: PASS");
}
