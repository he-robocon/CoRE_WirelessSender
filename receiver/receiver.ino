#include <M5Unified.h>
#include <Ethernet.h>
#include <EthernetUdp.h>
#include <utility/w5100.h>
#include <SPI.h>

#include <CoreLanProtocol.h>
#include <CoreControllerUi.h>

namespace {

uint8_t receiverMac[] = {0x02, 0x43, 0x4F, 0x52, 0x45, 0x20};
EthernetUDP udp;
M5Canvas uiCanvas(&M5.Display);
bool uiCanvasReady = false;

bool ethernetReady = false;
bool hasSequence = false;
uint32_t nextScreenMs = 0;
uint32_t nextNetworkRetryMs = 0;
uint32_t nextDebugMs = 0;
uint32_t nextRawDebugMs = 0;
uint32_t receivedCount = 0;
uint32_t replyCount = 0;
uint32_t invalidCount = 0;
uint32_t lostCount = 0;
uint32_t lastSequence = 0;
uint32_t lastReceiveMs = 0;
IPAddress lastRemoteIp;
uint16_t lastRemotePort = 0;
constexpr size_t RAW_BUFFER_SIZE = 64;
uint8_t lastRawData[RAW_BUFFER_SIZE] = {};
size_t lastRawLength = 0;
int lastDatagramLength = 0;
uint8_t lastHidReport[core_lan::CONTROLLER_REPORT_SIZE] = {};
uint8_t lastHidReportLength = 0;

const char* hardwareStatusText(EthernetHardwareStatus status) {
  switch (status) {
    case EthernetW5500:
      return "W5500";
    case EthernetNoHardware:
      return "NOT FOUND";
    default:
      return "UNKNOWN";
  }
}

const char* linkStatusText(EthernetLinkStatus status) {
  switch (status) {
    case LinkON:
      return "LINK UP";
    case LinkOFF:
      return "LINK DOWN";
    default:
      return "UNKNOWN";
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
  // Read W5500 state before startWrite(). The display and W5500 share the SPI
  // bus, so Ethernet SPI transactions must not be nested in a display write.
  const EthernetHardwareStatus hardwareStatus = Ethernet.hardwareStatus();
  const EthernetLinkStatus linkStatus = Ethernet.linkStatus();
  const bool linkUp = linkStatus == LinkON;
  const uint8_t phyConfig = readPhyConfig();

  if (!uiCanvasReady) return;

  uiCanvas.fillSprite(TFT_BLACK);
  uiCanvas.setCursor(8, 4);
  uiCanvas.setTextSize(2);
  uiCanvas.setTextColor(TFT_CYAN, TFT_BLACK);
  uiCanvas.println("Controller Receiver");
  uiCanvas.setTextSize(1);
  uiCanvas.setTextColor(linkUp ? TFT_GREEN : TFT_RED, TFT_BLACK);
  uiCanvas.printf("LAN %s  %s %s  W5500:%s\n", linkStatusText(linkStatus),
                  phySpeedText(phyConfig), phyDuplexText(phyConfig),
                  hardwareStatusText(hardwareStatus));
  uiCanvas.setTextColor(ethernetReady ? TFT_GREEN : TFT_RED, TFT_BLACK);
  uiCanvas.printf("UDP %s  RX:%lu  ACK:%lu  LOST:%lu\n",
                  ethernetReady ? "READY" : "ERROR",
                  static_cast<unsigned long>(receivedCount),
                  static_cast<unsigned long>(replyCount),
                  static_cast<unsigned long>(lostCount));
  uiCanvas.setTextColor(TFT_WHITE, TFT_BLACK);
  uiCanvas.print("RAW: ");
  if (lastHidReportLength == 0) {
    uiCanvas.println("---");
  } else {
    for (uint8_t i = 0; i < lastHidReportLength; ++i) {
      uiCanvas.printf("%02X%s", lastHidReport[i],
                      i + 1 == lastHidReportLength ? "\n" : " ");
    }
  }

  const core_ui::ControllerState controller =
      core_ui::decode(lastHidReport, lastHidReportLength);
  core_ui::drawController(uiCanvas, controller, 62);

  uiCanvas.setCursor(8, 166);
  if (receivedCount > 0) {
    uiCanvas.printf("FROM %u.%u.%u.%u:%u  SEQ:%lu\n",
                    lastRemoteIp[0], lastRemoteIp[1], lastRemoteIp[2],
                    lastRemoteIp[3], lastRemotePort,
                    static_cast<unsigned long>(lastSequence));
  } else {
    uiCanvas.println("FROM ---.---.---.---  SEQ:---");
  }
  uiCanvas.printf("LOCAL 192.168.10.20:%u  INVALID:%lu\n",
                  core_lan::RECEIVER_PORT,
                  static_cast<unsigned long>(invalidCount));

  const bool receiving = receivedCount > 0 &&
                         millis() - lastReceiveMs <= core_lan::RECEIVE_TIMEOUT_MS;
  uiCanvas.setCursor(8, 201);
  uiCanvas.setTextSize(2);
  uiCanvas.setTextColor(receiving ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  uiCanvas.println(receiving ? "RECEIVING" : "WAITING DATA");
  uiCanvas.pushSprite(0, 0);
}

void initializeEthernet() {
  udp.stop();
  ethernetReady = false;

  pinMode(core_lan::LAN_CS_PIN, OUTPUT);
  digitalWrite(core_lan::LAN_CS_PIN, HIGH);
  core_lan::resetLanModule();

  SPI.begin(core_lan::SPI_SCLK_PIN,
            core_lan::SPI_MISO_PIN,
            core_lan::SPI_MOSI_PIN,
            core_lan::LAN_CS_PIN);
  Ethernet.init(core_lan::LAN_CS_PIN);
  Ethernet.begin(receiverMac,
                 core_lan::receiverIp(),
                 core_lan::dnsIp(),
                 core_lan::gatewayIp(),
                 core_lan::subnetMask());

  // Keep configuration writes conservative on the CoreS3 M5-Bus.
  uint8_t configuredIp[] = {192, 168, 10, 20};
  uint8_t configuredGateway[] = {192, 168, 10, 1};
  uint8_t configuredSubnet[] = {255, 255, 255, 0};
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
  W5100.setMACAddress(receiverMac);
  W5100.setIPAddress(configuredIp);
  W5100.setGatewayIp(configuredGateway);
  W5100.setSubnetMask(configuredSubnet);
  SPI.endTransaction();

  ethernetReady = Ethernet.hardwareStatus() != EthernetNoHardware &&
                  udp.begin(core_lan::RECEIVER_PORT) == 1;

  const IPAddress actualIp = Ethernet.localIP();
  Serial.printf("Ethernet init: hardware=%s link=%s udp=%s\n",
                hardwareStatusText(Ethernet.hardwareStatus()),
                linkStatusText(Ethernet.linkStatus()),
                ethernetReady ? "ready" : "error");
  Serial.print("Configured IP readback: ");
  core_lan::printIp(Serial, actualIp);
  Serial.println();
}

void recordSequence(uint32_t sequence) {
  // During startup the sender can already be far ahead while the receiver is
  // rebooting or being flashed. Use the first packets only to establish the
  // current sequence baseline.
  if (receivedCount <= 500) {
    lastSequence = sequence;
    hasSequence = true;
    lostCount = 0;
    return;
  }
  if (hasSequence) {
    const uint32_t distance = sequence - lastSequence;
    if (distance > 1 && distance < 0x80000000UL) {
      lostCount += distance - 1;
    }
  }
  lastSequence = sequence;
  hasSequence = true;
}

void sendControllerAck(core_lan::ControllerPacket packet,
                       const IPAddress& remoteIp,
                       uint16_t remotePort) {
  packet.type = core_lan::PacketType::ControllerAck;
  if (udp.beginPacket(remoteIp, remotePort) == 1 &&
      udp.write(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)) == sizeof(packet) &&
      udp.endPacket() == 1) {
    ++replyCount;
  }
}

void receiveControllerData() {
  int packetSize = 0;
  while ((packetSize = udp.parsePacket()) > 0) {
    const IPAddress remoteIp = udp.remoteIP();
    const uint16_t remotePort = udp.remotePort();
    uint8_t rawData[RAW_BUFFER_SIZE] = {};
    const int bytesRead = udp.read(rawData, sizeof(rawData));
    udp.flush();

    lastDatagramLength = packetSize;
    lastRawLength = bytesRead > 0 ? static_cast<size_t>(bytesRead) : 0;
    memcpy(lastRawData, rawData, lastRawLength);

    core_lan::ControllerPacket packet{};
    if (lastRawLength >= sizeof(packet)) {
      memcpy(&packet, rawData, sizeof(packet));
    }

    if (packetSize == sizeof(packet) &&
        bytesRead == sizeof(packet) &&
        core_lan::isValidControllerPacket(
            packet, core_lan::PacketType::Controller)) {
      ++receivedCount;
      lastReceiveMs = millis();
      lastRemoteIp = remoteIp;
      lastRemotePort = remotePort;
      const bool reportChanged =
          packet.reportLength != lastHidReportLength ||
          memcmp(lastHidReport, packet.report, packet.reportLength) != 0;
      lastHidReportLength = packet.reportLength;
      memcpy(lastHidReport, packet.report, lastHidReportLength);
      const uint32_t now = millis();
      if (reportChanged &&
          static_cast<int32_t>(now - nextRawDebugMs) >= 0) {
        nextRawDebugMs = now + 50;
        Serial.printf("RX HID RAW %u bytes from %u.%u.%u.%u:%u:",
                      lastHidReportLength, remoteIp[0], remoteIp[1],
                      remoteIp[2], remoteIp[3], remotePort);
        for (uint8_t i = 0; i < lastHidReportLength; ++i) {
          Serial.printf(" %02X", lastHidReport[i]);
        }
        Serial.println();
      }
      recordSequence(packet.sequence);
      sendControllerAck(packet, remoteIp, remotePort);
    } else {
      ++invalidCount;
      Serial.printf("Invalid UDP packet: datagram=%d read=%d\n",
                    packetSize, bytesRead);
    }
  }
}

}  // namespace

void setup() {
  auto config = M5.config();
  config.internal_spk = false;  // GPIO0 is used as the LAN reset signal.
  M5.begin(config);

  M5.Display.setRotation(1);
  uiCanvas.setColorDepth(8);
  uiCanvasReady =
      uiCanvas.createSprite(M5.Display.width(), M5.Display.height()) != nullptr;
  Serial.begin(115200);
  initializeEthernet();

  Serial.println("Core LAN receiver started");
  Serial.print("Local IP: ");
  core_lan::printIp(Serial, Ethernet.localIP());
  Serial.println();
  drawScreen();
}

void loop() {
  M5.update();
  const uint32_t now = millis();

  if (ethernetReady) {
    receiveControllerData();
  } else if (static_cast<int32_t>(now - nextNetworkRetryMs) >= 0) {
    nextNetworkRetryMs = now + core_lan::NETWORK_RETRY_MS;
    initializeEthernet();
  }

  if (static_cast<int32_t>(now - nextScreenMs) >= 0) {
    nextScreenMs = now + core_lan::SCREEN_REFRESH_MS;
    drawScreen();
  }

  if (static_cast<int32_t>(now - nextDebugMs) >= 0) {
    nextDebugMs = now + 1000;
    const uint8_t phyConfig = readPhyConfig();
    Serial.printf("link=%s phy=%s/%s(0x%02X) udp=%s receive=%lu invalid=%lu reply=%lu lost=%lu\n",
                  linkStatusText(Ethernet.linkStatus()),
                  phySpeedText(phyConfig), phyDuplexText(phyConfig), phyConfig,
                  ethernetReady ? "ready" : "error",
                  static_cast<unsigned long>(receivedCount),
                  static_cast<unsigned long>(invalidCount),
                  static_cast<unsigned long>(replyCount),
                  static_cast<unsigned long>(lostCount));
  }

  delay(1);
}
