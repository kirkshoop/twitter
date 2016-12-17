#pragma once

namespace rximgui {

inline float  Clamp(float v, float mn, float mx)                       { return (v < mn) ? mn : (v > mx) ? mx : v; }
inline ImVec2 Clamp(const ImVec2& f, const ImVec2& mn, ImVec2 mx)      { return ImVec2(Clamp(f.x,mn.x,mx.x), Clamp(f.y,mn.y,mx.y)); }

subjects::subject<int> framebus;
auto frameout = framebus.get_subscriber();
auto sendframe = []() {
    frameout.on_next(1);
};
auto frames = framebus.get_observable();

schedulers::run_loop rl;

}
