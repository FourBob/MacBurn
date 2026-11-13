// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/RenderList.hpp"

namespace mb {

namespace {
constexpr Color kBurn{0.48f, 0.26f, 0.13f, 1};
constexpr Color kTravel{0.04f, 0.42f, 1.0f, 0.7f};
}

RenderList buildRenderList(const Document& doc, const Job& job, Phase phase, Rect workArea) {
    RenderList rl;
    rl.workArea = workArea;

    if (phase == Phase::Prepare) {
        Vec2 pos{0, 0};
        Stroke burn{{}, false, kBurn, 1.2f, false};
        for (const auto& m : job.moves) {
            if (m.kind == Move::Kind::Rapid) {
                if (burn.points.size() > 1) rl.strokes.push_back(burn);
                burn.points.clear();
                rl.strokes.push_back({{pos, m.to}, false, kTravel, 0.6f, true});
            } else {
                if (burn.points.empty()) burn.points.push_back(pos);
                burn.points.push_back(m.to);
            }
            pos = m.to;
        }
        if (burn.points.size() > 1) rl.strokes.push_back(burn);
        return rl;
    }

    for (const auto& s : doc.shapes) {
        if (s.layer >= doc.layers.size()) continue;
        const Layer& l = doc.layers[s.layer];
        Color c = phase == Phase::Design ? l.color : kBurn;
        if (!l.output) c.a = 0.3f;
        rl.strokes.push_back({s.path.points, s.path.closed, c, 1.0f, false});
    }
    return rl;
}

} // namespace mb
