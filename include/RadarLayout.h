#pragma once

constexpr int DISPLAY_W = 320;
constexpr int DISPLAY_H = 240;
constexpr int RADAR_SIZE = 240;
constexpr int RADAR_CENTRE = RADAR_SIZE / 2 - 1;
constexpr int RADAR_RADIUS = RADAR_SIZE / 2 - 1;
constexpr int SIDEBAR_X = RADAR_SIZE;
constexpr int SIDEBAR_W = DISPLAY_W - RADAR_SIZE;

struct ButtonRect { int x, y, w, h; };
constexpr ButtonRect BTN_FW_UPDATE = {218, 198, 96, 32};
constexpr ButtonRect BTN_PING      = {156, 198, 56, 32};
