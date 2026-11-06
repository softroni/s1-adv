#include "tv_ble.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#include <esp_heap_caps.h>
#include <esp_wifi.h>

#include "tv_remote.h"

namespace tv_ble {
namespace {

constexpr uint16_t kAppearanceKeyboard = 0x03C1;
// The host may read two reports sent in one connection event as one press, so each waits a little.
constexpr uint32_t kReportGapMs = 8;
// Below this the stack's own allocations could leave too little for the screen and the network,
// and if it leaves less than the second once it's up, it goes again rather than starve the rest.
constexpr uint32_t kMinFreeHeap = 60000;
constexpr uint32_t kMinHeapLeft = 24000;

NimBLEServer* gServer = nullptr;
NimBLEHIDDevice* gHid = nullptr;
NimBLECharacteristic* gKeys = nullptr;
NimBLECharacteristic* gMedia = nullptr;
volatile bool gConnected = false;
bool gOn = false;
bool gFailed = false;
uint32_t gHeapCost = 0;
const char* gFailure = "";

// The stack's own task calls these, so they only set flags.
class Callbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override { gConnected = true; }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override { gConnected = false; }
};
Callbacks gCallbacks;

// Wi-Fi and Bluetooth share one radio, and the Wi-Fi library aborts if modem sleep is off while
// Bluetooth is enabled ("Should enable WiFi modem sleep when both WiFi and Bluetooth are
// enabled"). The firmware turns it off for speed in several places, so while Bluetooth holds it,
// a request for none gets the lightest sleep instead, and the last mode asked for comes back after.
volatile bool gHoldingWifiSleep = false;
wifi_ps_type_t gWifiSleepWanted = WIFI_PS_NONE;

bool send(NimBLECharacteristic* c, const uint8_t* data, size_t length) {
  if (!gOn || !gConnected || !c) return false;
  const bool sent = c->notify(data, length);
  delay(kReportGapMs);
  return sent;
}

}  // namespace
}  // namespace tv_ble

extern "C" esp_err_t __real_esp_wifi_set_ps(wifi_ps_type_t type);
extern "C" esp_err_t __wrap_esp_wifi_set_ps(wifi_ps_type_t type) {
  if (tv_ble::gHoldingWifiSleep) {
    tv_ble::gWifiSleepWanted = type;
    if (type == WIFI_PS_NONE) type = WIFI_PS_MIN_MODEM;
  }
  return __real_esp_wifi_set_ps(type);
}

namespace tv_ble {
namespace {
void holdWifiSleep() {
  if (gHoldingWifiSleep) return;
  wifi_ps_type_t now = WIFI_PS_NONE;
  gWifiSleepWanted = esp_wifi_get_ps(&now) == ESP_OK ? now : WIFI_PS_NONE;
  gHoldingWifiSleep = true;
  if (gWifiSleepWanted == WIFI_PS_NONE) __real_esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
}
void releaseWifiSleep() {
  if (!gHoldingWifiSleep) return;
  gHoldingWifiSleep = false;
  __real_esp_wifi_set_ps(gWifiSleepWanted);
}
}  // namespace

bool start(const char* name, int batteryPercent) {
  if (gOn) return true;
  gFailed = false;
  gFailure = "";
  const uint32_t before = ESP.getFreeHeap();
  if (before < kMinFreeHeap) {
    gFailed = true;
    gFailure = "Not enough memory for Bluetooth";
    return false;
  }
  holdWifiSleep();  // Before the controller is enabled, so there's no moment with both and no sleep.
  if (!NimBLEDevice::init(name)) {
    NimBLEDevice::deinit(true);
    gFailed = true;
    gFailure = "Bluetooth didn't start";
    return false;
  }
  // Just Works pairing, remembered: the Apple TV asks for no code, and reconnects by itself.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  gServer = NimBLEDevice::createServer();
  gServer->setCallbacks(&gCallbacks, false);
  gServer->advertiseOnDisconnect(true);

  gHid = new NimBLEHIDDevice(gServer);
  gKeys = gHid->getInputReport(1);
  gHid->getOutputReport(1);  // The host's lights. Nothing here shows them.
  gMedia = gHid->getInputReport(2);
  gHid->setManufacturer("M5Stack");
  gHid->setPnp(0x02, 0x303A, 0x8150, 0x0100);
  gHid->setHidInfo(0x00, 0x01);
  gHid->setReportMap(const_cast<uint8_t*>(tv_remote::kReportMap), sizeof(tv_remote::kReportMap));
  gHid->setBatteryLevel(batteryPercent < 0 ? 100 : batteryPercent > 100 ? 100 : uint8_t(batteryPercent));
  gServer->start();

  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setAppearance(kAppearanceKeyboard);
  advertising->addServiceUUID(gHid->getHidService()->getUUID());
  advertising->setName(name);
  advertising->enableScanResponse(true);
  if (!advertising->start()) {
    stop();
    gFailed = true;
    gFailure = "Bluetooth couldn't advertise";
    return false;
  }
  gOn = true;
  const uint32_t after = ESP.getFreeHeap();
  gHeapCost = before > after ? before - after : 0;
  if (after < kMinHeapLeft) {
    stop();
    gFailed = true;
    gFailure = "Not enough memory left for Bluetooth";
    return false;
  }
  return true;
}

void stop() {
  if (NimBLEDevice::isInitialized()) {
    if (NimBLEDevice::getAdvertising()) NimBLEDevice::getAdvertising()->stop();
  }
  // The server, its services and the advertising go with the stack; the HID object only points at them.
  NimBLEDevice::deinit(true);
  releaseWifiSleep();  // Only once the controller is off.
  delete gHid;
  gHid = nullptr;
  gServer = nullptr;
  gKeys = gMedia = nullptr;
  gConnected = false;
  gOn = false;
}

State state() {
  if (gFailed) return State::Failed;
  if (!gOn) return State::Off;
  return gConnected ? State::Connected : State::Waiting;
}
bool on() { return gOn; }
uint32_t heapCost() { return gHeapCost; }
const char* failure() { return gFailure; }
bool paired() { return gOn && NimBLEDevice::getNumBonds() > 0; }

bool forget() {
  if (!gOn) return false;
  if (gConnected && gServer) {
    for (uint16_t handle : gServer->getPeerDevices()) gServer->disconnect(handle);
  }
  return NimBLEDevice::deleteAllBonds();
}

bool key(uint8_t usage, uint8_t modifiers) {
  if (!usage) return false;
  const uint8_t press[8] = {modifiers, 0, usage, 0, 0, 0, 0, 0};
  const uint8_t release[8] = {};
  const bool sent = send(gKeys, press, sizeof(press));
  send(gKeys, release, sizeof(release));
  return sent;
}

bool media(uint16_t usage) {
  if (!usage) return false;
  const uint8_t press[2] = {uint8_t(usage & 0xFF), uint8_t(usage >> 8)};
  const uint8_t release[2] = {};
  const bool sent = send(gMedia, press, sizeof(press));
  send(gMedia, release, sizeof(release));
  return sent;
}

}  // namespace tv_ble
