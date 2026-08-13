#pragma once

#include <LovyanGFX.hpp>

void DrawScanLines(LGFX_Sprite& buf, const int x0, const int y0, const int x1, const int y1, const int thickness, const int trailBrightness, const int spacing)
{
    float dx = x1 - x0;
    float dy = y1 - y0;
    float armLen = sqrt(dx * dx + dy * dy);

    float px = -dy / armLen;
    float py = dx / armLen;

    auto clampToRadius = [&](float tipX, float tipY) -> std::pair<int,int> {
        float tdx = tipX - x0, tdy = tipY - y0;
        float dist = sqrt(tdx * tdx + tdy * tdy);
        if (dist > armLen) { tipX = x0 + tdx * (armLen / dist); tipY = y0 + tdy * (armLen / dist); }
        return { (int)tipX, (int)tipY };
    };

    for (int i = 0; i <= thickness; i++) {
        float t = i / (float)(thickness);
        uint8_t brightness = (uint8_t)(t * trailBrightness);
        auto [tx, ty] = clampToRadius(x1 + px * (i * spacing), y1 + py * (i * spacing));
        buf.drawLine(x0, y0, tx, ty, lgfx::color888(0, brightness, 0));
    }

    auto [tx, ty] = clampToRadius(x1 + px * (thickness * spacing), y1 + py * (thickness * spacing));
    buf.drawLine(x0, y0, tx, ty, lgfx::color888(0, 200, 0));
}
