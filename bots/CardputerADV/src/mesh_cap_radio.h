#pragma once
#include "mesh_contacts.h"
#include "mesh_help.h"
#include "mesh_leaf_state.h"
#include "mesh_private_channel.h"
#include "mesh_outbox.h"
#include "mesh_region.h"
#include "radio_scan.h"
#include <RadioLib.h>
#include <libmeshtastic_leaf.h>
#include <atomic>

namespace s1_mesh {
constexpr int kInitPending = -32768;
// The most M5 rates the Cap for. Meshtastic's US limit is 30 dBm, so this is well inside it.
constexpr int8_t kTxPowerDbm = 20;
// The radio while Mesh is open. It joins the mesh as soon as Mesh opens and leaves only when Mesh
// closes, so every Mesh screen runs on top of it. Radio Scan reads the chat's own channel through it,
// and borrows it (scanning) only to sweep the band or listen with another preset.
struct RadioSession {
  Module module;
  SX1262 radio;
  libmeshtastic_leaf::libmeshtastic_leaf mesh;
  State state;
  std::atomic<int> initResult{kInitPending};
  bool readyReported=false;
  uint32_t lowHeap=UINT32_MAX, lowLargest=UINT32_MAX;
  const RegionCode region;
  s1_scan::Scanner scan;  // The sweep and the other presets, while it's borrowed.
  bool scanning = false;  // Lent to Radio Scan: the chat hears nothing until it's back.
  // Everything the chat's radio hears, readable or not, and its channel's signal, for Radio Scan.
  s1_scan::Listen heard;
  s1_scan::Strip line;
  uint32_t sampleAt = 0;
  uint16_t unread = 0;    // Messages that came in while the chat wasn't on the screen.
  // Chat's side: who to message, what's going out, Help, and where the GPS last had a fix.
  Contacts contacts;
  Outbox outbox;
  Help help;
  Where where;
  PrivateChannel privateChannel;  // Heard and sent alongside the public channel, when there is one.
  uint32_t nextSeq = 1;
  char longName[kNameBytes+1] = "S1-ADV";
  char shortName[5] = "S1";
  bool historyDirty = false;
  bool announce = false;  // A radio said it lacks this one's key, so say who this is again.
  uint32_t savedMs = 0, publicLocationMs = 0, stopAskedMs = 0;
  RadioSession(SPIClass& spi,uint8_t savedRegion)
    :module(5,4,3,6,spi),radio(&module),region(regionOrDefault(savedRegion)) {
    module.spiConfig.timeout=250; // A missing/busy chip cannot stall the UI task.
  }
  int initialize() {
    // mesh.begin() sets the modem again, but start on the region's own channel: RadioLib only
    // recalibrates the chip for a move of 20 MHz or more, so 923.875 then 906.875 would keep
    // the calibration for the wrong band.
    int e=radio.begin(regionFrequency(region),250.0f,11,5,RADIOLIB_SX126X_SYNC_WORD_PRIVATE,kTxPowerDbm,16,1.8f);
    if(e) return e;
    if((e=radio.setDio2AsRfSwitch(true)))return e;
    if((e=radio.setCurrentLimit(140.0f)))return e;
    if((e=radio.setCRC(2)))return e;
    libmeshtastic_leaf::MeshConfig cfg;
    cfg.radio.region=region;
    cfg.radio.preset=libmeshtastic_leaf::PRESET_LONG_FAST;
    cfg.radio.txPower=kTxPowerDbm;
    cfg.hopLimit=3;
    mesh.setOwner(longName, shortName);
    return mesh.begin(cfg,&radio) ? 0 : RADIOLIB_ERR_UNKNOWN;
  }
};
// One allocation while Mesh opens, and the start needs a 24 KB free block, so keep well under it.
static_assert(sizeof(RadioSession) < 20 * 1024, "the Mesh session must fit the start's free-memory check");
} // namespace s1_mesh
