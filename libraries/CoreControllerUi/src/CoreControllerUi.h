#pragma once

#include <Arduino.h>
#include <M5Unified.h>

namespace core_ui {

struct ControllerState {
  bool a = false;
  bool b = false;
  bool x = false;
  bool y = false;
  bool l = false;
  bool r = false;
  bool zl = false;
  bool zr = false;
  bool minus = false;
  bool plus = false;
  bool home = false;
  bool capture = false;
  bool leftStick = false;
  bool rightStick = false;
  uint8_t dpad = 0x0F;
  uint8_t leftX = 0x80;
  uint8_t leftY = 0x80;
  uint8_t rightX = 0x80;
  uint8_t rightY = 0x80;
};

inline ControllerState decode(const uint8_t* report, uint8_t length) {
  ControllerState state;
  if (report == nullptr || length < 7) return state;

  state.y = report[0] & 0x01;
  state.b = report[0] & 0x02;
  state.a = report[0] & 0x04;
  state.x = report[0] & 0x08;
  state.l = report[0] & 0x10;
  state.r = report[0] & 0x20;
  state.zl = report[0] & 0x40;
  state.zr = report[0] & 0x80;

  state.minus = report[1] & 0x01;
  state.plus = report[1] & 0x02;
  state.leftStick = report[1] & 0x04;
  state.rightStick = report[1] & 0x08;
  state.home = report[1] & 0x10;
  state.capture = report[1] & 0x20;

  state.dpad = report[2] & 0x0F;
  state.leftX = report[3];
  state.leftY = report[4];
  state.rightX = report[5];
  state.rightY = report[6];
  return state;
}

inline const char* dpadName(uint8_t dpad) {
  switch (dpad) {
    case 0: return "UP";
    case 1: return "UP-R";
    case 2: return "RIGHT";
    case 3: return "DN-R";
    case 4: return "DOWN";
    case 5: return "DN-L";
    case 6: return "LEFT";
    case 7: return "UP-L";
    default: return "CENTER";
  }
}

template <typename Canvas>
inline void drawButtonText(Canvas& canvas, int16_t x, int16_t y,
                           const char* label, bool pressed) {
  canvas.setCursor(x, y);
  canvas.setTextColor(pressed ? TFT_BLACK : TFT_LIGHTGREY,
                      pressed ? TFT_GREEN : TFT_BLACK);
  canvas.printf(" %s ", label);
  canvas.setTextColor(TFT_WHITE, TFT_BLACK);
}

template <typename Canvas>
inline void drawStick(Canvas& canvas, int16_t centerX, int16_t centerY,
                      uint8_t x, uint8_t y, const char* label, bool pressed) {
  constexpr int16_t radius = 25;
  canvas.drawRect(centerX - radius, centerY - radius,
                  radius * 2, radius * 2, TFT_DARKGREY);
  canvas.drawFastHLine(centerX - radius, centerY, radius * 2, TFT_DARKGREY);
  canvas.drawFastVLine(centerX, centerY - radius, radius * 2, TFT_DARKGREY);
  const int16_t dotX = map(x, 0, 255, -radius + 2, radius - 2);
  const int16_t dotY = map(y, 0, 255, -radius + 2, radius - 2);
  canvas.fillCircle(centerX + dotX, centerY + dotY,
                    pressed ? 6 : 4, pressed ? TFT_YELLOW : TFT_GREEN);
  canvas.setCursor(centerX - 29, centerY + radius + 4);
  canvas.printf("%s %3u,%3u", label, x, y);
}

template <typename Canvas>
inline void drawDpad(Canvas& canvas, int16_t centerX, int16_t centerY,
                     uint8_t dpad) {
  canvas.drawRect(centerX - 8, centerY - 25, 16, 50, TFT_DARKGREY);
  canvas.drawRect(centerX - 25, centerY - 8, 50, 16, TFT_DARKGREY);
  int8_t dx = 0;
  int8_t dy = 0;
  switch (dpad) {
    case 0: dy = -1; break;
    case 1: dx = 1; dy = -1; break;
    case 2: dx = 1; break;
    case 3: dx = 1; dy = 1; break;
    case 4: dy = 1; break;
    case 5: dx = -1; dy = 1; break;
    case 6: dx = -1; break;
    case 7: dx = -1; dy = -1; break;
    default: break;
  }
  canvas.fillCircle(centerX + dx * 15, centerY + dy * 15,
                    dpad <= 7 ? 6 : 3,
                    dpad <= 7 ? TFT_YELLOW : TFT_DARKGREY);
  canvas.setCursor(centerX - 25, centerY + 29);
  canvas.printf("DP %s", dpadName(dpad));
}

template <typename Canvas>
inline void drawController(Canvas& canvas, const ControllerState& state,
                           int16_t topY = 60) {
  drawButtonText(canvas, 8, topY, "Y", state.y);
  drawButtonText(canvas, 40, topY, "B", state.b);
  drawButtonText(canvas, 72, topY, "A", state.a);
  drawButtonText(canvas, 104, topY, "X", state.x);
  drawButtonText(canvas, 148, topY, "L", state.l);
  drawButtonText(canvas, 180, topY, "R", state.r);
  drawButtonText(canvas, 220, topY, "ZL", state.zl);
  drawButtonText(canvas, 265, topY, "ZR", state.zr);

  drawButtonText(canvas, 8, topY + 14, "-", state.minus);
  drawButtonText(canvas, 40, topY + 14, "+", state.plus);
  drawButtonText(canvas, 72, topY + 14, "HOME", state.home);
  drawButtonText(canvas, 132, topY + 14, "CAP", state.capture);
  drawButtonText(canvas, 181, topY + 14, "LS", state.leftStick);
  drawButtonText(canvas, 220, topY + 14, "RS", state.rightStick);

  const int16_t graphY = topY + 53;
  drawStick(canvas, 48, graphY, state.leftX, state.leftY,
            "LS", state.leftStick);
  drawStick(canvas, 151, graphY, state.rightX, state.rightY,
            "RS", state.rightStick);
  drawDpad(canvas, 260, graphY, state.dpad);
}

}  // namespace core_ui
