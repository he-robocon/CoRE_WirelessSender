#include <M5Unified.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <SPI.h>

// usbhub.h includes the USB Host Shield library's local "Usb.h".
#include <usbhub.h>
#include <hiduniversal.h>

// Sender module DIP settings:
// LAN Module13.2 CS: M5-Bus 23 / CoreS3 GPIO13
// USB Module V1.2 SS: CH2 / CoreS3 GPIO1
// USB Module V1.2 INT: CH2 / CoreS3 GPIO14
#define CORE_LAN_CS_PIN 13
#define CORE_LAN_INT_PIN 10  // LAN interrupt is not used.
#define CORE_USB_CS_PIN 1
#include <CoreLanProtocol.h>
#include <CoreControllerUi.h>

namespace {

uint8_t senderMac[] = {0x02, 0x43, 0x4F, 0x52, 0x45, 0x10};
EthernetUDP udp;
USB Usb;
USBHub Hub(&Usb);

bool ethernetReady = false;
bool usbHostReady = false;
bool hasReport = false;
uint8_t latestReport[core_lan::CONTROLLER_REPORT_SIZE] = {};
uint8_t latestReportLength = 0;
uint32_t reportGeneration = 0;
uint32_t sentGeneration = 0;
uint32_t hidReportCount = 0;
uint32_t changedReportCount = 0;
uint32_t nextScreenMs = 0;
uint32_t nextNetworkRetryMs = 0;
uint32_t nextDebugMs = 0;
uint32_t nextRawDebugMs = 0;
uint32_t nextSequence = 1;
uint32_t sentCount = 0;
uint32_t sendErrorCount = 0;
uint32_t ackCount = 0;
uint32_t invalidCount = 0;
uint32_t lastAckMs = 0;
uint32_t lastRttMs = 0;

void printRawReport(const uint8_t* report, uint8_t length) {
  Serial.printf("HID RAW %u bytes:", length);
  for (uint8_t i = 0; i < length; ++i) {
    Serial.printf(" %02X", report[i]);
  }
  Serial.println();
}

class ControllerHID : public HIDUniversal {
 public:
  explicit ControllerHID(USB* usb) : HIDUniversal(usb) {}

 protected:
  void ParseHIDData(USBHID* hid, bool hasReportId, uint8_t length,
                    uint8_t* report) override {
    (void)hid;
    (void)hasReportId;
    ++hidReportCount;

    const uint8_t storedLength =
        min<uint8_t>(length, core_lan::CONTROLLER_REPORT_SIZE);
    const bool changed = !hasReport || storedLength != latestReportLength ||
                         memcmp(latestReport, report, storedLength) != 0;
    latestReportLength = storedLength;
    memcpy(latestReport, report, storedLength);
    hasReport = true;
    ++reportGeneration;

    const uint32_t now = millis();
    if (changed && static_cast<int32_t>(now - nextRawDebugMs) >= 0) {
      nextRawDebugMs = now + 50;
      ++changedReportCount;
      printRawReport(latestReport, latestReportLength);
    } else if (changed) {
      ++changedReportCount;
    }
  }
};

ControllerHID Hid(&Usb);
M5Canvas uiCanvas(&M5.Display);
bool uiCanvasReady = false;
constexpr uint32_t SENDER_UI_REFRESH_MS = 100;

const char* hardwareStatusText(EthernetHardwareStatus status) {
  switch (status) {
    case EthernetW5500: return "W5500";
    case EthernetNoHardware: return "NOT FOUND";
    default: return "UNKNOWN";
  }
}

const char* linkStatusText(EthernetLinkStatus status) {
  switch (status) {
    case LinkON: return "LINK UP";
    case LinkOFF: return "LINK DOWN";
    default: return "UNKNOWN";
  }
}

uint8_t readPhyConfig() {
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  const uint8_t value = W5100.readPHYCFGR_W5500();
  SPI.endTransaction();
  return value;
}

const char* phySpeedText(uint8_t phyConfig) {
  return (phyConfig & 0x02) ? "100 Mbps" : "10 Mbps";
}

const char* phyDuplexText(uint8_t phyConfig) {
  return (phyConfig & 0x04) ? "FULL" : "HALF";
}

void drawIp(const char* label, const IPAddress& ip) {
  M5.Display.printf("%-7s", label);
  core_lan::printIp(M5.Display, ip);
  M5.Display.println();
}

void drawScreen() {
  const EthernetHardwareStatus hardwareStatus = Ethernet.hardwareStatus();
  const EthernetLinkStatus linkStatus = Ethernet.linkStatus();
  const uint8_t phyConfig = readPhyConfig();
  const uint8_t usbState = Usb.getUsbTaskState();
  const bool hidRunning = usbHostReady && usbState == USB_STATE_RUNNING;

  if (!uiCanvasReady) return;

  uiCanvas.fillSprite(TFT_BLACK);
  uiCanvas.setCursor(8, 4);
  uiCanvas.setTextSize(2);
  uiCanvas.setTextColor(TFT_CYAN, TFT_BLACK);
  uiCanvas.println("Controller Sender");
  uiCanvas.setTextSize(1);
  uiCanvas.setTextColor(linkStatus == LinkON ? TFT_GREEN : TFT_RED, TFT_BLACK);
  uiCanvas.printf("LAN %s  %s %s  UDP %s\n", linkStatusText(linkStatus),
                  phySpeedText(phyConfig), phyDuplexText(phyConfig),
                  ethernetReady ? "READY" : "ERROR");
  uiCanvas.setTextColor(hidRunning ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  uiCanvas.printf("USB %s  HID:%lu  W5500:%s\n",
                  hidRunning ? "RUNNING" : "WAITING",
                  static_cast<unsigned long>(hidReportCount),
                  hardwareStatusText(hardwareStatus));
  uiCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  uiCanvas.print("RAW: ");
  for (uint8_t i = 0; i < latestReportLength; ++i) {
    uiCanvas.printf("%02X%s", latestReport[i],
                    i + 1 == latestReportLength ? "\n" : " ");
  }

  const core_ui::ControllerState controller =
      core_ui::decode(latestReport, latestReportLength);
  core_ui::drawController(uiCanvas, controller, 62);

  uiCanvas.setCursor(8, 166);
  uiCanvas.printf("TX:%lu  ACK:%lu  ERR:%lu  RTT:%lums\n",
                  static_cast<unsigned long>(sentCount),
                  static_cast<unsigned long>(ackCount),
                  static_cast<unsigned long>(sendErrorCount),
                  static_cast<unsigned long>(lastRttMs));
  uiCanvas.printf("192.168.10.10 -> 192.168.10.20  CHG:%lu\n",
                  static_cast<unsigned long>(changedReportCount));

  const bool streaming = hidRunning && hasReport && sentCount > 0 &&
                         millis() - lastAckMs <= core_lan::RECEIVE_TIMEOUT_MS;
  uiCanvas.setCursor(8, 201);
  uiCanvas.setTextSize(2);
  uiCanvas.setTextColor(streaming ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  uiCanvas.println(streaming ? "STREAMING" : "WAITING");
  uiCanvas.pushSprite(0, 0);
}

void initializeEthernet() {
  udp.stop();
  ethernetReady = false;

  pinMode(core_lan::USB_CS_PIN, OUTPUT);
  digitalWrite(core_lan::USB_CS_PIN, HIGH);
  pinMode(core_lan::LAN_CS_PIN, OUTPUT);
  digitalWrite(core_lan::LAN_CS_PIN, HIGH);
  core_lan::resetLanModule();

  SPI.begin(core_lan::SPI_SCLK_PIN, core_lan::SPI_MISO_PIN,
            core_lan::SPI_MOSI_PIN, core_lan::LAN_CS_PIN);
  Ethernet.init(core_lan::LAN_CS_PIN);
  Ethernet.begin(senderMac, core_lan::senderIp(), core_lan::dnsIp(),
                 core_lan::gatewayIp(), core_lan::subnetMask());

  uint8_t configuredIp[] = {192, 168, 10, 10};
  uint8_t configuredGateway[] = {192, 168, 10, 1};
  uint8_t configuredSubnet[] = {255, 255, 255, 0};
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  W5100.setMACAddress(senderMac);
  W5100.setIPAddress(configuredIp);
  W5100.setGatewayIp(configuredGateway);
  W5100.setSubnetMask(configuredSubnet);
  SPI.endTransaction();

  ethernetReady = Ethernet.hardwareStatus() != EthernetNoHardware &&
                  udp.begin(core_lan::SENDER_PORT) == 1;
}

void initializeUsb() {
  pinMode(core_lan::LAN_CS_PIN, OUTPUT);
  digitalWrite(core_lan::LAN_CS_PIN, HIGH);
  usbHostReady = Usb.Init() != -1;
  Serial.printf("USB host: %s REV=%02X state=%02X\n",
                usbHostReady ? "ready" : "error", Usb.regRd(rREVISION),
                Usb.getUsbTaskState());
}

void sendControllerReport() {
  core_lan::ControllerPacket packet{};
  packet.magic = core_lan::PACKET_MAGIC;
  packet.version = core_lan::PROTOCOL_VERSION;
  packet.type = core_lan::PacketType::Controller;
  packet.size = sizeof(packet);
  packet.sequence = nextSequence;
  packet.senderUptimeMs = millis();
  packet.reportLength = latestReportLength;
  memcpy(packet.report, latestReport, latestReportLength);

  if (udp.beginPacket(core_lan::receiverIp(), core_lan::RECEIVER_PORT) == 1 &&
      udp.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)) ==
          sizeof(packet) &&
      udp.endPacket() == 1) {
    ++sentCount;
    ++nextSequence;
    sentGeneration = reportGeneration;
  } else {
    ++sendErrorCount;
  }
}

void receiveAck() {
  int packetSize = 0;
  while ((packetSize = udp.parsePacket()) > 0) {
    core_lan::ControllerPacket packet{};
    const int bytesRead =
        udp.read(reinterpret_cast<uint8_t*>(&packet), sizeof(packet));
    udp.flush();
    if (packetSize == sizeof(packet) && bytesRead == sizeof(packet) &&
        core_lan::isValidControllerPacket(
            packet, core_lan::PacketType::ControllerAck)) {
      ++ackCount;
      lastAckMs = millis();
      lastRttMs = lastAckMs - packet.senderUptimeMs;
    } else {
      ++invalidCount;
    }
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_spk = false;
  M5.begin(config);
  M5.Display.setRotation(1);
  uiCanvas.setColorDepth(8);
  uiCanvasReady =
      uiCanvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  M5.Power.setExtOutput(true);
  Serial.begin(115200);

  initializeEthernet();
  initializeUsb();
  Serial.println("Controller LAN sender started");
  drawScreen();
}

void loop() {
  M5.update();
  const uint32_t now = millis();

  if (usbHostReady) {
    Usb.Task();
  }
  if (ethernetReady) {
    receiveAck();
    if (hasReport && reportGeneration != sentGeneration) {
      sendControllerReport();
    }
  } else if (static_cast<int32_t>(now - nextNetworkRetryMs) >= 0) {
    nextNetworkRetryMs = now + core_lan::NETWORK_RETRY_MS;
    initializeEthernet();
  }

  if (static_cast<int32_t>(now - nextScreenMs) >= 0) {
    nextScreenMs = now + SENDER_UI_REFRESH_MS;
    drawScreen();
  }
  if (static_cast<int32_t>(now - nextDebugMs) >= 0) {
    nextDebugMs = now + 1000;
    const uint8_t phyConfig = readPhyConfig();
    Serial.printf("usb=%02X hid=%lu link=%s phy=%s/%s(0x%02X) tx=%lu err=%lu ack=%lu rtt=%lu\n",
                  Usb.getUsbTaskState(),
                  static_cast<unsigned long>(hidReportCount),
                  linkStatusText(Ethernet.linkStatus()),
                  phySpeedText(phyConfig), phyDuplexText(phyConfig), phyConfig,
                  static_cast<unsigned long>(sentCount),
                  static_cast<unsigned long>(sendErrorCount),
                  static_cast<unsigned long>(ackCount),
                  static_cast<unsigned long>(lastRttMs));
  }
  delay(1);
}
