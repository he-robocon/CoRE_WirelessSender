#include <M5Unified.h>
// usbhub.h includes the USB Host Shield library's local "Usb.h".
#include <usbhub.h>
#include <hiduniversal.h>

namespace {

constexpr size_t MAX_REPORT_SIZE = 64;

USB Usb;
USBHub Hub(&Usb);

uint8_t lastReport[MAX_REPORT_SIZE] = {};
uint8_t lastReportLength = 0;
uint32_t reportCount = 0;
uint32_t changedReportCount = 0;
uint32_t nextScreenMs = 0;
uint32_t nextStatusMs = 0;
bool hostReady = false;
bool parserReady = false;

bool reportChanged(const uint8_t* report, uint8_t length) {
  return length != lastReportLength || memcmp(lastReport, report, length) != 0;
}

void printRawReport(const uint8_t* report, uint8_t length) {
  Serial.printf("HID RAW len=%u:", length);
  for (uint8_t i = 0; i < length; ++i) {
    Serial.printf(" %02X", report[i]);
  }
  Serial.println();
}

class ProbeHIDUniversal : public HIDUniversal {
 public:
  explicit ProbeHIDUniversal(USB* usb) : HIDUniversal(usb) {}

 protected:
  void ParseHIDData(USBHID* hid, bool hasReportId, uint8_t length,
                    uint8_t* report) override {
    (void)hid;
    (void)hasReportId;
    ++reportCount;

    const uint8_t storedLength = min<uint8_t>(length, MAX_REPORT_SIZE);
    if (reportChanged(report, storedLength)) {
      ++changedReportCount;
      lastReportLength = storedLength;
      memcpy(lastReport, report, storedLength);
      printRawReport(report, storedLength);
    }
  }
};

ProbeHIDUniversal Hid(&Usb);

const char* stateName(uint8_t state) {
  if ((state & USB_STATE_MASK) == USB_STATE_DETACHED) return "DETACHED";
  switch (state) {
    case USB_STATE_CONFIGURING: return "CONFIGURING";
    case USB_STATE_RUNNING: return "RUNNING";
    case USB_STATE_ERROR: return "ERROR";
    default: return "WAITING";
  }
}

void drawScreen() {
  const uint8_t taskState = Usb.getUsbTaskState();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setCursor(8, 8);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.println("USB HID Probe");
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(hostReady ? TFT_GREEN : TFT_RED, TFT_BLACK);
  M5.Display.printf("MAX3421E: %s  REV: %02X\n",
                    hostReady ? "READY" : "ERROR", Usb.regRd(rREVISION));
  M5.Display.setTextColor(taskState == USB_STATE_RUNNING ? TFT_GREEN : TFT_ORANGE,
                          TFT_BLACK);
  M5.Display.printf("USB: %s (0x%02X)  INT: %d\n", stateName(taskState),
                    taskState, digitalRead(14));
  M5.Display.printf("Bus: %u  HRSL: %02X  HIRQ: %02X\n", Usb.getVbusState(),
                    Usb.regRd(rHRSL), Usb.regRd(rHIRQ));
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.printf("HID parser: %s\n", parserReady ? "READY" : "ERROR");
  M5.Display.printf("Reports: %lu  Changed: %lu\n",
                    static_cast<unsigned long>(reportCount),
                    static_cast<unsigned long>(changedReportCount));
  M5.Display.printf("Raw: %u byte(s)\n", lastReportLength);

  const uint8_t shown = min<uint8_t>(lastReportLength, 40);
  for (uint8_t i = 0; i < shown; ++i) {
    M5.Display.printf("%02X%s", lastReport[i],
                      ((i + 1) % 8 == 0 || i + 1 == shown) ? "\n" : " ");
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_spk = false;
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Power.setExtOutput(true);

  Serial.begin(115200);
  delay(250);
  Serial.println("CoreS3 USB V1.2 UHS probe starting");
  Serial.println("Pins: SCK=36 MISO=35 MOSI=37 SS=1 INT=14");

  // Keep a possible LAN module deselected while probing USB by itself.
  pinMode(13, OUTPUT);
  digitalWrite(13, HIGH);

  hostReady = Usb.Init() != -1;
  Serial.printf("MAX3421E host: %s REV=%02X state=%02X bus=%u INT=%d HRSL=%02X HIRQ=%02X\n",
                hostReady ? "ready" : "error", Usb.regRd(rREVISION),
                Usb.getUsbTaskState(), Usb.getVbusState(), digitalRead(14),
                Usb.regRd(rHRSL), Usb.regRd(rHIRQ));

  if (hostReady) {
    parserReady = true;
    Serial.println("HID raw capture: ready");
  }
  drawScreen();
}

void loop() {
  M5.update();
  if (hostReady) {
    Usb.Task();
  }

  const uint32_t now = millis();
  if (hostReady && static_cast<int32_t>(now - nextStatusMs) >= 0) {
    nextStatusMs = now + 2000;
    Serial.printf("USB state=%02X bus=%u INT=%d HRSL=%02X HIRQ=%02X reports=%lu\n",
                  Usb.getUsbTaskState(), Usb.getVbusState(), digitalRead(14),
                  Usb.regRd(rHRSL), Usb.regRd(rHIRQ),
                  static_cast<unsigned long>(reportCount));
  }
  if (static_cast<int32_t>(now - nextScreenMs) >= 0) {
    nextScreenMs = now + 100;
    drawScreen();
  }
  delay(1);
}
