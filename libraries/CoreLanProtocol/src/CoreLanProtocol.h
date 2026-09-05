#pragma once

#include <Arduino.h>
#include <IPAddress.h>

namespace core_lan {

constexpr int SPI_SCLK_PIN = 36;
constexpr int SPI_MISO_PIN = 35;
constexpr int SPI_MOSI_PIN = 37;
// Module13.2 LAN can route CS to more than one M5-Bus position. A sketch may
// define CORE_LAN_CS_PIN before including this header to match its module.
#ifndef CORE_LAN_CS_PIN
#define CORE_LAN_CS_PIN 1
#endif

constexpr int LAN_CS_PIN = CORE_LAN_CS_PIN;
constexpr int LAN_RST_PIN = 0;
#ifndef CORE_LAN_INT_PIN
#define CORE_LAN_INT_PIN 10
#endif
constexpr int LAN_INT_PIN = CORE_LAN_INT_PIN;

// Reserved for the USB V1.2 module that will be enabled in the next stage.
// Keeping it high prevents the MAX3421E from driving the shared SPI bus.
#ifndef CORE_USB_CS_PIN
#define CORE_USB_CS_PIN 7
#endif
constexpr int USB_CS_PIN = CORE_USB_CS_PIN;

constexpr uint16_t RECEIVER_PORT = 5000;
constexpr uint16_t SENDER_PORT = 5001;
constexpr uint32_t PING_INTERVAL_MS = 100;
constexpr uint32_t RECEIVE_TIMEOUT_MS = 500;
constexpr uint32_t SCREEN_REFRESH_MS = 33;
constexpr uint32_t NETWORK_RETRY_MS = 2000;

constexpr uint32_t PACKET_MAGIC = 0x434F5245;  // "CORE"
constexpr uint8_t PROTOCOL_VERSION = 1;

enum class PacketType : uint8_t {
  Ping = 1,
  Pong = 2,
  Controller = 3,
  ControllerAck = 4,
};

#pragma pack(push, 1)
struct LanTestPacket {
  uint32_t magic;
  uint8_t version;
  PacketType type;
  uint16_t size;
  uint32_t sequence;
  uint32_t senderUptimeMs;
};

constexpr size_t CONTROLLER_REPORT_SIZE = 8;

struct ControllerPacket {
  uint32_t magic;
  uint8_t version;
  PacketType type;
  uint16_t size;
  uint32_t sequence;
  uint32_t senderUptimeMs;
  uint8_t reportLength;
  uint8_t reserved[3];
  uint8_t report[CONTROLLER_REPORT_SIZE];
};
#pragma pack(pop)

static_assert(sizeof(LanTestPacket) == 16, "Unexpected LAN test packet size");
static_assert(sizeof(ControllerPacket) == 28,
              "Unexpected controller packet size");

inline IPAddress senderIp() {
  return IPAddress(192, 168, 10, 10);
}

inline IPAddress receiverIp() {
  return IPAddress(192, 168, 10, 20);
}

inline IPAddress broadcastIp() {
  return IPAddress(192, 168, 10, 255);
}

inline IPAddress gatewayIp() {
  return IPAddress(192, 168, 10, 1);
}

inline IPAddress dnsIp() {
  return gatewayIp();
}

inline IPAddress subnetMask() {
  return IPAddress(255, 255, 255, 0);
}

inline bool isValidPacket(const LanTestPacket& packet, PacketType expectedType) {
  return packet.magic == PACKET_MAGIC &&
         packet.version == PROTOCOL_VERSION &&
         packet.type == expectedType &&
         packet.size == sizeof(LanTestPacket);
}

inline bool isValidControllerPacket(const ControllerPacket& packet,
                                    PacketType expectedType) {
  return packet.magic == PACKET_MAGIC &&
         packet.version == PROTOCOL_VERSION &&
         packet.type == expectedType &&
         packet.size == sizeof(ControllerPacket) &&
         packet.reportLength <= CONTROLLER_REPORT_SIZE;
}

inline void resetLanModule() {
  pinMode(LAN_RST_PIN, OUTPUT);
  digitalWrite(LAN_RST_PIN, LOW);
  delay(10);
  digitalWrite(LAN_RST_PIN, HIGH);
  delay(100);
}

inline void printIp(Print& output, const IPAddress& ip) {
  output.printf("%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

}  // namespace core_lan
