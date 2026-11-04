#pragma once
#include "mesh_contacts.h"
#include "mesh_help.h"
#include "mesh_leaf_state.h"
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
// Chat joins the mesh and sends. Scan only listens, so it never starts the mesh at all.
enum class Use : uint8_t { Chat, Scan };
struct RadioSession {
  Module module;
  SX1262 radio;
  libmeshtastic_leaf::libmeshtastic_leaf mesh;
  State state;
  std::atomic<int> initResult{kInitPending};
  bool readyReported=false;
  uint32_t lowHeap=UINT32_MAX, lowLargest=UINT32_MAX;
  const RegionCode region;
  const Use use;
  s1_scan::Scanner scan;
  // Chat's side: who to message, what's going out, Help, and where the GPS last had a fix.
  Contacts contacts;
  Outbox outbox;
  Help help;
  Where where;
  uint32_t nextSeq = 1;
  char longName[kNameBytes+1] = "S1-ADV";
  char shortName[5] = "S1";
  bool historyDirty = false;
  bool announce = false;  // A radio said it lacks this one's key, so say who this is again.
  uint32_t savedMs = 0, publicLocationMs = 0, stopAskedMs = 0;
  RadioSession(SPIClass& spi,uint8_t savedRegion,Use purpose)
    :module(5,4,3,6,spi),radio(&module),region(regionOrDefault(savedRegion)),use(purpose) {
    module.spiConfig.timeout=250; // A missing/busy chip cannot stall the UI task.
  }
  // The mesh puts the chip to sleep when it ends. Scan never started it, so sleep the chip here,
  // before the SD card gets the bus back. The lifecycle only frees a session once init returned.
  ~RadioSession() {
    if(use==Use::Scan && initResult.load(std::memory_order_acquire)==0) radio.sleep();
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
    if(use==Use::Scan)return 0;
    libmeshtastic_leaf::MeshConfig cfg;
    cfg.radio.region=region;
    cfg.radio.preset=libmeshtastic_leaf::PRESET_LONG_FAST;
    cfg.radio.txPower=kTxPowerDbm;
    cfg.hopLimit=3;
    mesh.setOwner(longName, shortName);
    return mesh.begin(cfg,&radio) ? 0 : RADIOLIB_ERR_UNKNOWN;
  }
};
} // namespace s1_mesh
