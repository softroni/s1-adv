// Meshtastic leaf mode owns EXT SPI while active. No radio at ordinary boot. Opening Mesh starts the
// radio and closing it stops it, so every Mesh screen runs on top of a radio that's hearing.
namespace {
bool gMeshNaming = false, gMeshOpenHelp = false;
char gMeshNameInput[s1_mesh::kNameBytes + 1] = {};
// The first screen is a list, so Enter opens the row picked; the letters still open theirs directly.
enum class MeshStartRow : uint8_t { Chat, Help, Scan, Name, Private };
constexpr uint8_t kMeshStartRows = 5;
uint8_t gMeshStartRow = 0;
// Which Mesh screen is up: the first screen, the chat (with People, Help and a message opened over
// it), or Radio Scan. The radio is the same under all three.
enum class MeshView : uint8_t { Hub, Chat, Live };
MeshView gMeshView = MeshView::Hub;
bool gMeshHelpFromHub = false;  // Help opened from the first screen goes back there.
// A start waiting for a background check (an update check, the weather or the inbox) or the start-up
// services to finish. They end by themselves, so the radio starts once they have.
bool gMeshStartWaiting = false;
// For Mesh's sounds: whether this radio has come up (so stopping it says so), and when the GPS last
// had a fix (so finding one says so, but a fix lost under a tree for a moment doesn't).
bool gMeshRadioUp = false, gMeshFixHeard = false;
uint32_t gMeshFixLastMs = 0;
// The contacts whose radios have pinged since Mesh started, so each pings once.
uint32_t gMeshPinged[s1_mesh::kContacts] = {};
bool meshFirstPing(uint32_t id) {
  for(const uint32_t p:gMeshPinged)if(p==id)return false;
  for(uint32_t& p:gMeshPinged)if(!p) {p=id;return true;}
  return false;
}
// Help is still repeating, so the first Esc only warns: the radio going off would end it.
constexpr uint32_t kMeshStopConfirmMs = 4000;
// Ctrl+L on the public channel asks once more, since everyone hears it.
constexpr uint32_t kMeshPublicLocationMs = 4000;

// The chat list is on the screen, so a message arriving there is seen.
bool meshSeeingChat() {
  if(gUiMode!=UiMode::Mesh || gMeshView!=MeshView::Chat || gMeshNaming || gMeshChannelSetup || !gMeshSession)return false;
  const auto& st=gMeshSession->state;
  return !st.helpView && !st.nodeView && !st.detailView;
}
void meshChatSeen() {if(gMeshSession)gMeshSession->unread=0;}

// Esc goes back one step: out of whatever is open, a borrowed radio back to the chat, the chat or
// Radio Scan back to the first screen. Only the first screen leaves Mesh, which turns the radio
// off, so it asks once first.
bool handleMeshEscape() {
  if(gUiMode!=UiMode::Mesh || gLauncherVisible)return false;
  if(gMeshNaming) {gMeshNaming=false;return true;}
  if(meshChannelEscape())return true;
  if(gMeshSession) {
    auto& s=*gMeshSession;auto& st=s.state;
    const bool ready=s.initResult.load(std::memory_order_acquire)==0 && !gMeshCancelStart;
    if(ready && handleRadioScanEscape(s))return true;
    if(gMeshView==MeshView::Chat) {
      if(st.detailView) {st.detailView=false;meshChatSeen();return true;}
      if(st.nodeView) {st.nodeView=false;meshChatSeen();return true;}
      if(st.helpView) {
        st.helpView=false;
        if(gMeshHelpFromHub)gMeshView=MeshView::Hub;else meshChatSeen();
        gMeshHelpFromHub=false;return true;
      }
      gMeshView=MeshView::Hub;return true;
    }
    if(gMeshView==MeshView::Live) {gMeshView=MeshView::Hub;return true;}
    if(ready && (!s.stopAskedMs || millis()-s.stopAskedMs>=kMeshStopConfirmMs)) {
      s.stopAskedMs=max<uint32_t>(millis(),1);
      gMeshNotice=s.help.repeating()?"Help is on. Esc again ends it":"Esc again turns the radio off";
      gMeshNoticeUntilMs=millis()+kMeshStopConfirmMs;
      meshSound(s1_mesh::Sound::Ask);
      return true;
    }
  }
  gMeshStartWaiting=false;
  if(stopMeshMode(true))setUiMode(UiMode::Home);
  return true;
}

bool meshBlocksMode(UiMode mode) {
  switch(mode) {
    case UiMode::Voice: case UiMode::Library: case UiMode::AudioFolders:
    case UiMode::Notes: case UiMode::Assets: case UiMode::Files:
    case UiMode::Nes: case UiMode::RfidLab: case UiMode::Sonar:
      return true;
    default:return false;
  }
}

// What the radio can't start beside, and what to say. A background check and the start-up services
// finish by themselves, so a start waits for those (meshStartWaitsFor); the rest are things you're
// doing, and Mesh says which to finish.
bool meshStartWaitsFor() {return gInboxHttpBusy || gBootBackgroundActive;}
const char* meshStartBlocker() {
  if(radioBusy())return "Stop the radio or podcast first";
  if(gOtaApplyInProgress || gTransferUiActive)return "Wait for the update to finish";
  if(gRealtimeActive || gRealtimeDialBusy || gRealtimeExclusiveMode || gPendingRealtimeStart)return "End the voice chat first";
  if(gRecording || gG0VoicePending || gVoiceCapturePreparing)return "Finish recording first";
  if(gSubmitting || gThinking || gVoiceDialBusy || gVoiceReplySpeechBusy || !gQueuedVoiceReplySpeech.isEmpty() ||
     gTextTurnTaskHandle || gCompletedTextTurn || !gPendingTextTurns.empty() || !gActiveTextTurn.isEmpty())
    return "Wait for Claude's reply";
  if(gPlaybackActive)return "Stop what's playing first";
  return nullptr;
}

// Touch only P0; never reset the I2C bus, expander or the other seven pins.
bool meshCapBit(uint8_t reg, bool value) {
  uint8_t byte=0;
  if(!gMeshCapBus || !gMeshCapBus->readRegister(0x43,reg,&byte,1,100000)) return false;
  byte=value ? uint8_t(byte|1) : uint8_t(byte&~1);
  return gMeshCapBus->writeRegister8(0x43,reg,byte,100000);
}

void meshRestoreCap() {
  if(gMeshCapBitsSaved) {
    meshCapBit(0x05,gMeshCapBits[1]);
    meshCapBit(0x03,gMeshCapBits[0]);
    meshCapBit(0x07,gMeshCapBits[2]);
  }
  gMeshCapBitsSaved=false;gMeshCapBus=nullptr;
}

bool meshEnableCap() {
  m5::I2C_Class* candidates[]={&m5::In_I2C,&m5::Ex_I2C};
  for(auto* bus:candidates) {
    if(!bus->isEnabled() || bus->getSDA()!=8 || bus->getSCL()!=9)continue;
    uint8_t bits[3]={};
    if(!bus->readRegister(0x43,0x03,&bits[0],1,100000) ||
       !bus->readRegister(0x43,0x05,&bits[1],1,100000) ||
       !bus->readRegister(0x43,0x07,&bits[2],1,100000))continue;
    gMeshCapBus=bus;gMeshCapBitsSaved=true;
    for(size_t i=0;i<3;++i)gMeshCapBits[i]=bits[i]&1;
    if(meshCapBit(0x05,true) && meshCapBit(0x03,true) && meshCapBit(0x07,false))return true;
    meshRestoreCap();return false;
  }
  return false;
}

bool loadMeshIdentity(s1_mesh::RadioSession& session) {
  uint8_t identity[64]={}, regenerated[32]={};
  Preferences prefs;
  if(!prefs.begin("s1mesh",false))return false;
  bool loaded=prefs.getBytesLength("identity")==sizeof(identity) &&
    prefs.getBytes("identity",identity,sizeof(identity))==sizeof(identity);
  bool valid=loaded && libmeshtastic_leaf::MeshCryptoPKI::isUsablePublicKey(identity) &&
    libmeshtastic_leaf::MeshCryptoPKI::regeneratePublicKey(regenerated,identity+32) &&
    memcmp(regenerated,identity,32)==0;
  if(!valid) {
    valid=libmeshtastic_leaf::libmeshtastic_leaf::generateKeyPair(identity,identity+32) &&
      prefs.putBytes("identity",identity,sizeof(identity))==sizeof(identity);
  }
  prefs.end();
  if(valid) {
    session.mesh.setMyPublicKey(identity);
    session.mesh.setMyPrivateKey(identity+32);
  }
  volatile uint8_t* wipe=identity;
  for(size_t i=0;i<sizeof(identity);++i)wipe[i]=0;
  return valid;
}

void meshRadioInitTask(void* pointer) {
  auto* session=static_cast<s1_mesh::RadioSession*>(pointer);
  const int result=session->initialize();
  session->initResult.store(result,std::memory_order_release);
  vTaskDelete(nullptr); // No session access after publishing the result.
}

bool stopMeshMode(bool goHome) {
  if(!gMeshBusOwned)return true;
  if(gMeshSession && gMeshSession->initResult.load(std::memory_order_acquire)==s1_mesh::kInitPending) {
    gMeshCancelStart=true;gMeshExitAfterStop|=goHome;gMeshNotice="Stopping radio...";
    return false;
  }
  if(gMeshSession && gMeshSession->readyReported)logf("[MESH] session min_heap=%u min_largest=%u",
    unsigned(gMeshSession->lowHeap),unsigned(gMeshSession->lowLargest));
  if(gMeshSession && gMeshSession->initResult.load(std::memory_order_acquire)==0 && gMeshSession->historyDirty)
    meshWriteHistory(gMeshSession->state);
  meshGps(false);
  gMeshSession.reset(); // Clears the IRQ callback and sleeps before SD resumes.
  digitalWrite(kRfidCsPin,HIGH);
  meshRestoreCap();
  gMeshBusOwned=false;gMeshCancelStart=false;gMeshExitAfterStop=false;
  gSdReady=mountSdCardOnSharedSpi();
  gStorageReady=gLittleFsReady || gSdReady;
  gMeshNotice=gSdReady ? "Radio off / SD ready" : "Radio off / SD unavailable";gMeshNoticeUntilMs=0;
  if(gMeshRadioUp)meshSound(s1_mesh::Sound::Off);
  gMeshRadioUp=false;
  logf("[MESH] stopped sd=%d heap=%u",gSdReady,unsigned(ESP.getFreeHeap()));
  return true;
}

void startMeshMode() {
  const bool openHelp=gMeshOpenHelp;gMeshOpenHelp=false;
  if(gMeshBusOwned)return;
  // Each way it can't start says why, with the same knock.
  const auto refuse=[](const char* why){gMeshNotice=why;meshSound(s1_mesh::Sound::Nope);};
  if(const char* why=meshStartBlocker()) {gMeshStartWaiting=false;refuse(why);return;}
  if(meshStartWaitsFor()) {
    // Back from Home, an update check or the weather has often just begun. Waiting beats a refusal.
    gMeshStartWaiting=true;gMeshOpenHelp=openHelp;
    if(gInboxHttpBusy)gInboxCancelRequested=true;  // One not sent yet is dropped, and asked again later.
    gMeshNotice="Finishing a background check";gMeshNoticeUntilMs=0;
    return;
  }
  gMeshStartWaiting=false;
  if(!gCanvasSpriteActive) {refuse("Display buffer recovering");return;}
  if(ESP.getFreeHeap()<65536 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<24576) {
    refuse("Not enough free RAM");return;
  }
  // Busy/heap failures leave SD and radio untouched.
  gMeshSession.reset(new(std::nothrow) s1_mesh::RadioSession(gSdSpi,gDeviceSettings.meshRegion));
  if(!gMeshSession) {refuse("Not enough free RAM");return;}
  gMeshRadioUp=false;gMeshFixHeard=false;memset(gMeshPinged,0,sizeof(gMeshPinged));
  gMeshNotice="Preparing radio...";render();
  if(!loadMeshIdentity(*gMeshSession)) {
    gMeshSession.reset();refuse("Identity storage failed");return;
  }
  // The name it announces, who it can message, and what was said before it last stopped.
  meshLoadProfile(*gMeshSession);
  meshLoadPrivateChannel(*gMeshSession);
  meshLoadHistory(gMeshSession->state);
  for(size_t i=0;i<gMeshSession->state.messageCount;++i)
    gMeshSession->nextSeq=max<uint32_t>(gMeshSession->nextSeq,gMeshSession->state.messages[i].seq+1);
  gMeshSession->state.helpView=openHelp;
  if(!meshEnableCap()) {
    gMeshSession.reset();refuse("Cap-1262 not detected");return;
  }
  gMeshBusOwned=true;gMeshCancelStart=false;gMeshExitAfterStop=false;gMeshNoticeUntilMs=0;
  gSdReady=false;gStorageReady=false;
  SD.end();
  digitalWrite(kSdCsPin,HIGH);pinMode(kSdCsPin,OUTPUT);
  digitalWrite(kRfidCsPin,HIGH);pinMode(kRfidCsPin,OUTPUT);
  gSdSpi.begin(kSdSckPin,kSdMisoPin,kSdMosiPin,kSdCsPin);
  gMeshNotice="Starting / SD paused";
  if(xTaskCreate(meshRadioInitTask,"mesh-init",8192,gMeshSession.get(),1,nullptr)!=pdPASS) {
    gMeshSession->initResult.store(RADIOLIB_ERR_UNKNOWN,std::memory_order_release);
    stopMeshMode(false);refuse("Radio task allocation failed");
  }
}

// The history and Help hear how each message the outbox sent turned out.
struct MeshSink {
  s1_mesh::RadioSession& s;
  void status(uint32_t seq,s1_mesh::Delivery d,uint8_t reason,uint32_t id) {
    auto* m=s.state.bySeq(seq);
    const bool changed=m && m->status!=d;
    if(m) {if(id)m->id=id;m->status=d;m->reason=reason;s.historyDirty=true;}
    bool help=false;
    for(const uint32_t q:s.help.seq)help=help||(q && q==seq);
    if(d==s1_mesh::Delivery::Delivered) {
      const bool wasOn=s.help.repeating();s.help.delivered(seq);
      if(wasOn && !s.help.repeating()) {meshWakeScreen();meshSound(s1_mesh::Sound::HelpAnswered);return;}
    }
    // How each message turned out, by ear. Help's own messages have Help's sounds instead.
    if(changed && !help && m->outgoing) {
      if(d==s1_mesh::Delivery::Delivered)meshSound(s1_mesh::Sound::Delivered);
      else if(d==s1_mesh::Delivery::Heard && !m->direct)meshSound(s1_mesh::Sound::Heard);
      else if(d==s1_mesh::Delivery::Failed || d==s1_mesh::Delivery::NoAnswer || d==s1_mesh::Delivery::NotConfirmed)
        meshSound(s1_mesh::Sound::Failed);
    }
    if(d==s1_mesh::Delivery::Failed && reason==35)s.announce=true;  // PKI_UNKNOWN_PUBKEY
  }
};
// The outbox's view of the library.
struct MeshRadioPort {
  decltype(s1_mesh::RadioSession::mesh)& mesh;
  uint32_t send(const s1_mesh::Outgoing& o) {
    const auto port=static_cast<meshtastic_PortNum>(o.port);
    if(o.pki)return mesh.sendDataPKI(port,o.data,o.len,o.to,o.key,o.reliable);
    if(o.channel==1)return mesh.sendDataOnSecondary(port,o.data,o.len,o.reliable);
    return mesh.sendData(port,o.data,o.len,o.to,o.reliable);
  }
  uint8_t lastResult() const {return uint8_t(mesh.getLastSendResult());}
  bool pending() const {return mesh.hasPendingAck();}
  bool transmitting() const {return mesh.isTransmitting();}
};

// Who's at a contact slot: 0 is the public channel, then each contact.
const s1_mesh::Contact* meshTargetContact(const s1_mesh::RadioSession& s,uint8_t target) {
  return target && target<=s.contacts.count ? &s.contacts.list[target-1] : nullptr;
}
// The order Left and Right step through: public, the private channel when there is one, then each
// contact. The private channel has a fixed number, so adding a contact can't move it.
uint8_t meshNextTarget(const s1_mesh::RadioSession& s,uint8_t target,bool back) {
  uint8_t order[s1_mesh::kContacts+2];size_t count=0,at=0;
  order[count++]=0;
  if(s.privateChannel.set())order[count++]=s1_mesh::kTargetPrivate;
  for(size_t i=0;i<s.contacts.count;++i)order[count++]=uint8_t(i+1);
  for(size_t i=0;i<count;++i)if(order[i]==target)at=i;
  return order[(at+(back?count-1:1))%count];
}
// Puts a text in the outbox and the history. False when the outbox is full.
bool meshQueueText(s1_mesh::RadioSession& s,uint8_t target,const char* text,uint32_t* seqOut=nullptr) {
  const bool privateChannel=target==s1_mesh::kTargetPrivate;
  if(privateChannel && !s.privateChannel.set())return false;
  const auto* contact=privateChannel?nullptr:meshTargetContact(s,target);
  if(target && !privateChannel && !contact)return false;
  s1_mesh::Outgoing o;
  o.seq=s.nextSeq;o.to=contact?contact->id:s1_mesh::kBroadcast;o.pki=contact!=nullptr;o.channel=privateChannel?1:0;
  if(contact)memcpy(o.key,contact->key,32);
  o.len=uint8_t(min<size_t>(strlen(text),s1_mesh::kTextBytes));memcpy(o.data,text,o.len);
  if(!s.outbox.push(o))return false;
  s.state.queued(s.mesh.getNodeNum(),o.to,o.pki,o.seq,text).privateChannel=privateChannel;
  ++s.nextSeq;s.historyDirty=true;
  if(seqOut)*seqOut=o.seq;
  return true;
}

// Who a Help round goes to: every contact, the private channel unless C turned it off, and the
// public channel when P asks for it or there's nobody else to send to.
struct MeshHelpPlan {bool privateChannel=false,publicChannel=false;};
MeshHelpPlan meshHelpPlan(const s1_mesh::RadioSession& s) {
  MeshHelpPlan plan;
  plan.privateChannel=s.privateChannel.set() && s.help.alsoPrivate;
  plan.publicChannel=s.help.alsoPublic || (!s.contacts.count && !plan.privateChannel);
  return plan;
}
// One Help round. Contacts and the private channel are people you chose, so they get exactly
// where the GPS last had a fix. The public channel gets what Settings > Device > Public location
// allows, since anyone in range hears it and radios with internet put it on public maps.
void meshHelpRound(s1_mesh::RadioSession& s,uint32_t now) {
  const auto plan=meshHelpPlan(s);
  const auto sharing=s1_mesh::sharingOrDefault(gDeviceSettings.meshPublicLocation);
  char text[s1_mesh::kTextBytes+1];
  s1_mesh::composeHelp(text,sizeof(text),s.help.choice,s.where,now);
  s.help.seq={};
  for(size_t i=0;i<s.contacts.count;++i) {uint32_t seq=0;if(meshQueueText(s,uint8_t(i+1),text,&seq))s.help.seq[i]=seq;}
  if(plan.privateChannel) {uint32_t seq=0;if(meshQueueText(s,s1_mesh::kTargetPrivate,text,&seq))s.help.seq[s1_mesh::kHelpPrivateSlot]=seq;}
  if(plan.publicChannel) {
    s1_mesh::composeHelp(text,sizeof(text),s.help.choice,s.where,now,sharing);
    uint32_t seq=0;if(meshQueueText(s,0,text,&seq))s.help.seq[s1_mesh::kHelpPublicSlot]=seq;
  }
  if(s.help.rounds)meshSound(s1_mesh::Sound::HelpRound);  // The first went out with Help's own beeps.
  s.help.roundSent(now);
  logf("[MESH] help round=%u choice=%u contacts=%u private=%d public=%d public_location=%s fix=%d",unsigned(s.help.rounds),
    unsigned(s.help.choice),unsigned(s.contacts.count),s.help.seq[s1_mesh::kHelpPrivateSlot]!=0,
    s.help.seq[s1_mesh::kHelpPublicSlot]!=0,s1_mesh::sharingName(sharing),s1_mesh::usable(s.where,now));
}

// What Chat does on every pass once the radio is up.
void meshServiceMessaging(s1_mesh::RadioSession& s,uint32_t now) {
  meshGps(true);
  meshReadGps(now,s.where);
  // A fix found, or found again after two minutes without one.
  if(s.where.known && now-s.where.atMs<5000) {
    if(!gMeshFixHeard || now-gMeshFixLastMs>=s1_mesh::kFixFreshMs)meshSound(s1_mesh::Sound::GpsFix);
    gMeshFixHeard=true;gMeshFixLastMs=now;
  }
  if(s.help.due(now,s.outbox.idle()))meshHelpRound(s,now);
  MeshRadioPort port{s.mesh};MeshSink sink{s};
  s.outbox.service(now,port,sink);
  // A radio that couldn't read a direct message lacked this one's key: announce it again.
  if(s.announce && s.outbox.idle() && !s.mesh.isTransmitting() && !s.mesh.hasPendingAck() && s.mesh.sendNodeInfo())s.announce=false;
  if(s.historyDirty && now-s.savedMs>=1000) {meshWriteHistory(s.state);s.historyDirty=false;s.savedMs=now;}
  meshScreenService(now,s.help.repeating());
}

// The public keys a direct message is decrypted with: a contact's saved key first, then one heard.
bool meshKeyFor(libmeshtastic_leaf::NodeNum id,uint8_t key[32]) {
  if(!gMeshSession)return false;
  if(const auto* c=gMeshSession->contacts.find(id)) {memcpy(key,c->key,32);return true;}
  const auto* n=gMeshSession->state.node(id);
  if(n && n->hasKey) {memcpy(key,n->key,32);return true;}
  return false;
}

void meshHandlePacket(const libmeshtastic_leaf::MeshPacket& packet) {
  if(!gMeshSession || packet.header.from==gMeshSession->mesh.getNodeNum())return;
  auto& s=*gMeshSession;
  auto& state=s.state;
  const uint32_t me=s.mesh.getNodeNum(),now=millis();
  // A contact's radio, heard for the first time since Mesh started, pings.
  if(s.contacts.find(packet.header.from) && meshFirstPing(packet.header.from))meshSound(s1_mesh::Sound::Nearby);
  auto* node=state.hear(packet.header.from,now,packet.rxRssi);
  if(packet.portNum==meshtastic_PortNum_NODEINFO_APP && node) {
    meshtastic_User user=meshtastic_User_init_zero;
    auto stream=pb_istream_from_buffer(packet.payload,packet.payloadLen);
    if(pb_decode(&stream,meshtastic_User_fields,&user)) {
      const char* name=user.long_name[0] ? user.long_name : user.short_name;
      s1_mesh::copyText(node->name,sizeof(node->name),reinterpret_cast<const uint8_t*>(name),strlen(name));
      if(user.public_key.size==32 && libmeshtastic_leaf::MeshCryptoPKI::isUsablePublicKey(user.public_key.bytes)) {
        memcpy(node->key,user.public_key.bytes,32);node->hasKey=true;
        const auto* c=s.contacts.find(packet.header.from);
        if(c && memcmp(c->key,node->key,32))logf("[MESH] contact !%08lx announced a different key; kept the saved one",
          static_cast<unsigned long>(packet.header.from));
      }
      if(s.contacts.rename(packet.header.from,node->name))meshSaveContacts(s.contacts);
    }
  }
  if(packet.portNum==meshtastic_PortNum_ROUTING_APP && packet.header.to==me && packet.requestId) {
    meshtastic_Routing routing=meshtastic_Routing_init_zero;
    auto stream=pb_istream_from_buffer(packet.payload,packet.payloadLen);
    if(pb_decode(&stream,meshtastic_Routing_fields,&routing) && routing.which_variant==meshtastic_Routing_error_reason_tag) {
      const uint8_t error=uint8_t(routing.error_reason);
      MeshSink sink{s};
      // An answer that came after the outbox moved on still counts.
      if(!s.outbox.routing(packet.header.from,packet.requestId,error,sink) && !error) {
        auto* m=state.byPacket(packet.requestId);
        if(m && m->direct && m->to==packet.header.from && m->status!=s1_mesh::Delivery::Delivered) {
          sink.status(m->seq,s1_mesh::Delivery::Delivered,0,m->id);
        }
      }
    }
  }
  if(packet.portNum==meshtastic_PortNum_TEXT_MESSAGE_APP && packet.payloadLen) {
    // Received radio text is display data, never an AI/device command.
    auto* m=state.message(packet.header.from,packet.header.id,false,packet.payload,packet.payloadLen);
    if(m) {
      m->to=packet.header.to;m->direct=packet.isPKI;s.historyDirty=true;
      m->privateChannel=!packet.isPKI && packet.channelIndex==1;
      const bool toMe=packet.header.to==me,contact=s.contacts.find(packet.header.from)!=nullptr;
      const bool helpWasOn=s.help.repeating();
      s.help.replied(contact || m->privateChannel,toMe,now);
      // Anywhere else in Mesh, it's counted on the first screen's Chat row and said at the bottom.
      if(!meshSeeingChat()) {
        if(s.unread<999)++s.unread;
        const auto* from=state.node(packet.header.from);
        char who[s1_mesh::kNameBytes+16];
        if(from && from->name[0])snprintf(who,sizeof(who),"Message from %s",from->name);
        else snprintf(who,sizeof(who),"Message from !%08lx",static_cast<unsigned long>(packet.header.from));
        gMeshNotice=who;gMeshNoticeUntilMs=now+4000;
      }
      // The private channel is only the people who share its key, so it's worth a sound too. Talk on
      // the public channel stays quiet.
      if(toMe || contact || m->privateChannel) {
        meshWakeScreen();
        meshSound(helpWasOn && !s.help.repeating()?s1_mesh::Sound::HelpAnswered:
          m->privateChannel && !toMe?s1_mesh::Sound::Group:s1_mesh::Sound::Message);
      }
    }
  }
}

void serviceMeshMode() {
  if(!gMeshBusOwned || !gMeshSession)return;
  const int result=gMeshSession->initResult.load(std::memory_order_acquire);
  if(result==s1_mesh::kInitPending)return;
  if(result || gMeshCancelStart) {
    const bool home=gMeshExitAfterStop;
    stopMeshMode(false);
    if(result) {gMeshNotice="Radio init failed: "+String(result);meshSound(s1_mesh::Sound::Failed);}
    if(home)setUiMode(UiMode::Home);
    return;
  }
  if(!gMeshRadioUp) {gMeshRadioUp=true;meshSound(s1_mesh::Sound::Ready);}
  auto& session=*gMeshSession;
  session.lowHeap=std::min(session.lowHeap,uint32_t(ESP.getFreeHeap()));
  session.lowLargest=std::min(session.lowLargest,uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  // Lent to Radio Scan: only the scan runs, and the chat waits for its radio back.
  if(session.scanning) {serviceRadioScan(session);meshScreenService(millis(),false);return;}
  if(!session.readyReported) {
    session.readyReported=true;
    session.line.reset(millis());  // Radio Scan's clock counts from here.
    logf("[MESH] ready %s LongFast %.3fMHz node=!%08lx tx=%ddBm contacts=%u session=%u heap=%u largest=%u",s1_mesh::regionName(session.region),
      double(s1_mesh::regionFrequency(session.region)),static_cast<unsigned long>(session.mesh.getNodeNum()),
      int(s1_mesh::kTxPowerDbm),unsigned(session.contacts.count),unsigned(sizeof(session)),unsigned(session.lowHeap),unsigned(session.lowLargest));
  }
  session.mesh.onReceive(meshHandlePacket);
  session.mesh.onReceivePKI(meshKeyFor);
  session.mesh.onHeard(meshHeard);
  session.mesh.update();
  serviceRadioLive(session,millis());
  meshServiceMessaging(session,millis());
  if(!gMeshNoticeUntilMs || int32_t(millis()-gMeshNoticeUntilMs)>=0)gMeshNotice="Listening / SD paused";
}

bool meshReady() {
  return gMeshSession && gMeshSession->initResult.load(std::memory_order_acquire)==0 && !gMeshCancelStart &&
    !gMeshSession->scanning;
}

bool meshSendInput() {
  if(!gMeshSession || gMeshSession->initResult.load(std::memory_order_acquire)!=0 || gMeshCancelStart)return false;
  auto& s=*gMeshSession;
  if(s.scanning)return false;  // Lent to Radio Scan, which never sends.
  if(!s.state.input[0])return true;
  if(!meshQueueText(s,s.state.target,s.state.input)) {
    gMeshNotice="Outbox full; wait a moment";gMeshNoticeUntilMs=millis()+2000;meshSound(s1_mesh::Sound::Nope);return false;
  }
  s.state.input[0]=0;s.state.historyOffset=0;s.state.detailView=false;s.state.nodeView=false;
  meshSound(s1_mesh::Sound::Sent);
  return true;
}

// Ctrl+L: where the GPS has the Cardputer, as a map pin for a Meshtastic app and as text with a
// map link, to whoever Enter sends to. A contact and the private channel get it exactly. The public
// channel gets it only as Settings > Device > Public location allows, and asks twice first.
void meshShareLocation() {
  if(!meshReady())return;
  auto& s=*gMeshSession;const uint32_t now=millis();
  const uint8_t target=s.state.target;
  // Every refusal knocks; the question before going public asks; a pin on its way sounds like a send.
  const auto refuse=[now](const char* why,uint32_t ms){gMeshNotice=why;gMeshNoticeUntilMs=now+ms;meshSound(s1_mesh::Sound::Nope);};
  if(target==s1_mesh::kTargetPrivate && !s.privateChannel.set()) {refuse("No private channel",2000);return;}
  const bool privateChannel=target==s1_mesh::kTargetPrivate;
  const auto* contact=privateChannel?nullptr:meshTargetContact(s,target);
  const bool publicChannel=!privateChannel && !contact;
  const auto sharing=publicChannel?s1_mesh::sharingOrDefault(gDeviceSettings.meshPublicLocation):s1_mesh::Sharing::Exact;
  if(sharing==s1_mesh::Sharing::Off) {refuse("Public location off: Settings",2500);return;}
  if(!s1_mesh::usable(s.where,now)) {refuse("No GPS fix yet: needs open sky",2500);return;}
  if(publicChannel && (!s.publicLocationMs || now-s.publicLocationMs>=kMeshPublicLocationMs)) {
    s.publicLocationMs=max<uint32_t>(now,1);
    gMeshNotice=sharing==s1_mesh::Sharing::Rough?"Public, within 3 km: C-L again":"Public, exact spot: C-L again";
    gMeshNoticeUntilMs=now+kMeshPublicLocationMs;meshSound(s1_mesh::Sound::Ask);return;
  }
  s.publicLocationMs=0;
  s1_mesh::Outgoing pin;pin.to=contact?contact->id:s1_mesh::kBroadcast;pin.pki=contact!=nullptr;pin.reliable=false;
  pin.channel=privateChannel?1:0;pin.port=s1_mesh::kPortPosition;if(contact)memcpy(pin.key,contact->key,32);
  pin.len=uint8_t(s1_mesh::encodePosition(pin.data,sizeof(pin.data),s.where.latE5,s.where.lonE5,s.where.satellites,
    sharing==s1_mesh::Sharing::Rough?s1_mesh::kRoughBits:s1_mesh::kExactBits));
  char text[s1_mesh::kTextBytes+1];s1_mesh::composeLocation(text,sizeof(text),s.where,now,sharing);
  if(s.outbox.queued()+2>s1_mesh::kOutbox || !s.outbox.push(pin) || !meshQueueText(s,target,text)) {
    refuse("Outbox full; wait a moment",2000);return;
  }
  gMeshNotice="Location queued";gMeshNoticeUntilMs=now+2000;meshSound(s1_mesh::Sound::Sent);
}

// The people list: contacts first, heard or not, then every other radio heard.
struct MeshPerson {uint32_t id=0;const char* name="";bool contact=false;const s1_mesh::Node* node=nullptr;};
size_t meshPeopleCount(const s1_mesh::RadioSession& s) {
  size_t n=s.contacts.count;
  for(size_t i=0;i<s.state.nodeCount;++i)if(!s.contacts.find(s.state.nodes[i].id))++n;
  return n;
}
bool meshPersonAt(const s1_mesh::RadioSession& s,size_t row,MeshPerson& out) {
  if(row<s.contacts.count) {
    const auto& c=s.contacts.list[row];
    out.id=c.id;out.name=c.name;out.contact=true;out.node=s.state.node(c.id);return true;
  }
  row-=s.contacts.count;
  for(size_t i=0;i<s.state.nodeCount;++i) {
    const auto& n=s.state.nodes[i];
    if(s.contacts.find(n.id))continue;
    if(!row--) {out.id=n.id;out.name=n.name;out.contact=false;out.node=&n;return true;}
  }
  return false;
}
void meshSaveContact() {
  auto& s=*gMeshSession;MeshPerson p;
  if(!meshPersonAt(s,s.state.nodeSel,p))return;
  const uint32_t now=millis();
  if(p.contact) {gMeshNotice="Already a contact. Del removes";gMeshNoticeUntilMs=now+2000;meshSound(s1_mesh::Sound::Nope);return;}
  if(!p.node || !p.node->hasKey) {gMeshNotice="No key heard yet: restart theirs";gMeshNoticeUntilMs=now+2500;meshSound(s1_mesh::Sound::Nope);return;}
  const auto added=s.contacts.add(p.id,p.node->name[0]?p.node->name:"",p.node->key);
  if(added==s1_mesh::Contacts::Add::Full) {gMeshNotice="Four contacts is the most";gMeshNoticeUntilMs=now+2000;meshSound(s1_mesh::Sound::Nope);return;}
  gMeshNotice=meshSaveContacts(s.contacts)?"Saved as a contact":"Saved until Mesh closes";gMeshNoticeUntilMs=now+2000;
  meshSound(s1_mesh::Sound::Saved);
  meshFirstPing(p.id);  // Just heard, so its next packet doesn't ping as if it had just come in range.
  s.state.nodeSel=s.contacts.indexOf(p.id)<0?0:size_t(s.contacts.indexOf(p.id));
}
void meshRemoveContact() {
  auto& s=*gMeshSession;MeshPerson p;
  if(!meshPersonAt(s,s.state.nodeSel,p) || !p.contact)return;
  const int index=s.contacts.indexOf(p.id);
  s.contacts.remove(p.id);meshSaveContacts(s.contacts);
  // Enter's target moves with the list, and goes back to the public channel if it was this one.
  if(index>=0 && s.state.target==uint8_t(index+1))s.state.target=0;
  else if(index>=0 && s.state.target>uint8_t(index+1))--s.state.target;
  const size_t people=meshPeopleCount(s);
  if(s.state.nodeSel>=people)s.state.nodeSel=people?people-1:0;
  gMeshNotice="Contact removed";gMeshNoticeUntilMs=millis()+2000;meshSound(s1_mesh::Sound::Removed);
}

bool meshHelpKey(char key,const Keyboard_Class::KeysState& keys) {
  auto& s=*gMeshSession;
  const char lower=char(tolower(static_cast<unsigned char>(key)));
  if(keys.ctrl && lower=='h') {s.state.helpView=false;return true;}
  if(key>='1' && key<='0'+char(s1_mesh::kHelpChoices)) {
    if(!meshReady()) {gMeshNotice="Radio still starting";gMeshNoticeUntilMs=millis()+1500;meshSound(s1_mesh::Sound::Nope);return true;}
    s.help.start(uint8_t(key-'1'),millis());s.stopAskedMs=0;
    meshSound(s1_mesh::Sound::HelpStart);
    logf("[MESH] help start choice=%u contacts=%u",unsigned(key-'1'),unsigned(s.contacts.count));
    return true;
  }
  // P and C switch a channel in or out of the next round: two clicks up for in, down for out.
  if(lower=='p') {
    s.help.alsoPublic=!s.help.alsoPublic;
    meshSound(s.help.alsoPublic?s1_mesh::Sound::Saved:s1_mesh::Sound::Removed);return true;
  }
  if(lower=='c') {
    if(s.privateChannel.set()) {
      s.help.alsoPrivate=!s.help.alsoPrivate;
      meshSound(s.help.alsoPrivate?s1_mesh::Sound::Saved:s1_mesh::Sound::Removed);
    } else {gMeshNotice="No private channel yet";gMeshNoticeUntilMs=millis()+2000;meshSound(s1_mesh::Sound::Nope);}
    return true;
  }
  return true;
}

bool meshNameKey(char key,const Keyboard_Class::KeysState& keys) {
  if(keys.ctrl)return true;
  if(keys.enter || key=='\r' || key=='\n') {
    char clean[s1_mesh::kNameBytes+1];s1_mesh::cleanName(clean,sizeof(clean),gMeshNameInput);
    const bool saved=meshSaveName(clean);
    // A running radio takes it at once and says who it is again.
    if(saved && gMeshSession && gMeshSession->initResult.load(std::memory_order_acquire)==0) {
      auto& s=*gMeshSession;meshSavedName(s.longName,s.shortName);
      s.mesh.setOwner(s.longName,s.shortName);s.announce=true;
    }
    gMeshNotice=saved?"Name saved":"Couldn't save the name";gMeshNoticeUntilMs=millis()+2000;
    meshSound(saved?s1_mesh::Sound::Saved:s1_mesh::Sound::Nope);
    gMeshNaming=false;return true;
  }
  if(keys.del || key=='\b') {
    size_t n=strlen(gMeshNameInput);
    if(n) {--n;while(n && (uint8_t(gMeshNameInput[n])&0xc0)==0x80)--n;gMeshNameInput[n]=0;}
    return true;
  }
  if(key>=32 && key<127) {
    const String text=translateInputChar(key);
    if(strlen(gMeshNameInput)+text.length()<=s1_mesh::kNameBytes)strcat(gMeshNameInput,text.c_str());
  }
  return true;
}

// The built-in keyboard sends no arrow codes: its arrows are ; . , / and Fn leaves them the same.
// A USB keyboard's arrows arrive as 1 to 4. Where there's a text box, the plain keys type, so only
// Fn with them moves; elsewhere they move either way. Gives 1 to 4, or 0 for any other key.
char meshArrow(char key,const Keyboard_Class::KeysState& keys,bool typing) {
  if(key>=1 && key<=4)return key;
  if(typing && !keys.fn)return 0;
  return key==';'?1:key=='.'?2:key==','?3:key=='/'?4:0;
}

void meshOpenName() {
  gMeshNaming=true;
  char shortName[5];meshSavedName(gMeshNameInput,shortName);
  if(!strcmp(gMeshNameInput,s1_mesh::kDefaultLongName))gMeshNameInput[0]=0;
}

// The start that was waiting, once nothing is in its way. Leaving Mesh drops it.
void serviceMeshStartWait() {
  if(!gMeshStartWaiting)return;
  if(gUiMode!=UiMode::Mesh || gMeshBusOwned || gMeshSession) {gMeshStartWaiting=false;return;}
  if(meshStartWaitsFor())return;
  gMeshStartWaiting=false;startMeshMode();
}

// Opening Mesh starts its radio, so it's hearing from the first screen on.
void beginMesh() {
  gMeshView=MeshView::Hub;gMeshHelpFromHub=false;gMeshStartWaiting=false;
  startMeshMode();
}

// A row opens its screen over the running radio. With no radio, because it couldn't start, it tries
// again; Name and Private channel don't need it.
void meshOpenStartRow(MeshStartRow row) {
  gMeshStartRow=uint8_t(row);
  auto* s=gMeshSession.get();
  switch(row) {
    case MeshStartRow::Chat: gMeshView=MeshView::Chat;meshChatSeen();break;
    case MeshStartRow::Help:
      gMeshView=MeshView::Chat;gMeshHelpFromHub=true;
      if(s) {s->state.helpView=true;s->state.nodeView=false;s->state.detailView=false;}
      else gMeshOpenHelp=true;
      break;
    case MeshStartRow::Scan: gMeshView=MeshView::Live;break;
    case MeshStartRow::Name: meshOpenName();return;
    case MeshStartRow::Private: meshChannelOpen();return;
  }
  if(!s && !gMeshStartWaiting)startMeshMode();
}

bool handleMeshKey(char key,const Keyboard_Class::KeysState& keys) {
  if(keys.ctrl && (key=='r'||key=='R'||key=='v'||key=='V')) {setStatus("Esc exits Mesh for voice",1600);return true;}
  const char lower=char(tolower(static_cast<unsigned char>(key)));
  if(keys.ctrl && lower!='n' && lower!='o' && lower!='l' && lower!='h')return false;
  if(gMeshNaming)return meshNameKey(key,keys);
  if(gMeshChannelSetup)return meshChannelKey(key,keys);
  if(!gMeshSession || gMeshView==MeshView::Hub) {
    if(keys.ctrl)return true;
    const char arrow=meshArrow(key,keys,false);
    if(arrow==1 || arrow==2) {
      gMeshStartRow=uint8_t((gMeshStartRow+(arrow==1?kMeshStartRows-1:1))%kMeshStartRows);
      // A notice from before gives way to what the row picked does, unless a start is still waiting.
      if(!gMeshStartWaiting && gMeshNotice!="Radio off / SD unavailable")gMeshNotice="";
      return true;
    }
    if(keys.enter || key=='\r' || key=='\n')meshOpenStartRow(MeshStartRow(gMeshStartRow%kMeshStartRows));
    else if(lower=='s')meshOpenStartRow(MeshStartRow::Scan);
    else if(lower=='h')meshOpenStartRow(MeshStartRow::Help);
    else if(lower=='c')meshOpenStartRow(MeshStartRow::Private);
    else if(lower=='n')meshOpenStartRow(MeshStartRow::Name);
    return true;
  }
  if(gMeshView==MeshView::Live) {
    if(keys.ctrl)return false;
    if(gMeshSession->initResult.load(std::memory_order_acquire)==0)handleRadioScanKey(key,keys);
    return true;
  }
  auto& s=*gMeshSession;auto& state=s.state;
  if(state.helpView)return meshHelpKey(key,keys);
  if(keys.ctrl && lower=='h') {state.helpView=true;state.nodeView=false;state.detailView=false;gMeshHelpFromHub=false;return true;}
  if(keys.ctrl && lower=='n') {state.nodeView=!state.nodeView;state.detailView=false;if(!state.nodeView)meshChatSeen();return true;}
  if(keys.ctrl && lower=='o') {state.detailView=!state.detailView;state.nodeView=false;if(!state.detailView)meshChatSeen();return true;}
  if(keys.ctrl && lower=='l') {meshShareLocation();return true;}
  // The chat has a text box, so its arrows need Fn. People and a message opened have none, so Up and
  // Down move there without it; Left and Right, which change who the box sends to, always need it.
  const char arrow=meshArrow(key,keys,true);
  const char upDown=state.nodeView || state.detailView?meshArrow(key,keys,false):arrow;
  if(arrow==3 || arrow==4) {state.target=meshNextTarget(s,state.target,arrow==3);return true;}
  if(upDown==1 || upDown==2) {
    if(state.nodeView) {
      const size_t people=meshPeopleCount(s);
      if(people)state.nodeSel=(state.nodeSel+(upDown==1?people-1:1))%people;
    } else if(state.messageCount) {
      state.historyOffset=(state.historyOffset+(upDown==1?1:state.messageCount-1))%state.messageCount;
    }
    return true;
  }
  if(state.nodeView && (keys.enter || key=='\r' || key=='\n')) {meshSaveContact();return true;}
  if(state.nodeView && (keys.del || key=='\b')) {meshRemoveContact();return true;}
  if(keys.enter || key=='\r' || key=='\n') {meshSendInput();return true;}
  if(keys.del || key=='\b') {state.erase();return true;}
  if(key>=32 && key<127) {
    const String text=translateInputChar(key);
    state.append(text.c_str());state.detailView=false;state.nodeView=false;meshChatSeen();
  }
  return true;
}

bool handleMeshHostCommand(const String& line) {
  if(!line.startsWith("mesh."))return false;
  // Opening Mesh starts the radio; start and scan open the chat or Radio Scan over it.
  if(line=="mesh.open")setUiMode(UiMode::Mesh);
  else if(line=="mesh.start") {setUiMode(UiMode::Mesh);if(gUiMode==UiMode::Mesh)meshOpenStartRow(MeshStartRow::Chat);}
  else if(line=="mesh.scan") {setUiMode(UiMode::Mesh);if(gUiMode==UiMode::Mesh)meshOpenStartRow(MeshStartRow::Scan);}
  else if(line=="mesh.stop") {if(stopMeshMode(true))setUiMode(UiMode::Home);}
  else if(line=="mesh.nodes") {if(gMeshSession)gMeshSession->state.nodeView=true;}
  else if(line=="mesh.status") {
    logf("[MESH] region=%s owned=%d init=%d sd=%d heap=%u largest=%u nodes=%u messages=%u",
      s1_mesh::regionName(gMeshSession?uint8_t(gMeshSession->region):gDeviceSettings.meshRegion),gMeshBusOwned,
      gMeshSession?gMeshSession->initResult.load():s1_mesh::kInitPending,gSdReady,unsigned(ESP.getFreeHeap()),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
      unsigned(gMeshSession?gMeshSession->state.nodeCount:0),unsigned(gMeshSession?gMeshSession->state.messageCount:0));
    if(gMeshSession)
      logf("[MESH] contacts=%u outbox=%u busy=%d help=%d round=%u stop=%u fix=%d",unsigned(gMeshSession->contacts.count),
        unsigned(gMeshSession->outbox.queued()),gMeshSession->outbox.busy(),gMeshSession->help.on,unsigned(gMeshSession->help.rounds),
        unsigned(gMeshSession->help.stop),s1_mesh::usable(gMeshSession->where,millis()));
    if(gMeshSession)logRadioScanStatus(*gMeshSession);
  }
  return true;
}

// --- Mesh drawing, shared with Radio Scan (part 087) and the private channel (part 091) ---------
// Font2 is twice Font0's height, so it carries every line. It has only ASCII, and a name or a message
// can be in any script, so anything else is drawn in efontCN_12, 2 px down to sit on the same line.
// "Small" is efontCN_12 throughout, for text that Font2 can't fit.
constexpr int kMeshTop=21,kMeshKeysY=119;
struct MeshInk {
  uint16_t bg=uint16_t(rgb565_local(3,8,14)),muted=uint16_t(rgb565_local(130,145,155)),white=uint16_t(TFT_WHITE);
  uint16_t cyan=uint16_t(rgb565_local(47,227,255)),green=uint16_t(rgb565_local(88,240,141));
  uint16_t purple=uint16_t(rgb565_local(157,102,255)),amber=uint16_t(rgb565_local(255,200,64));
  uint16_t orange=uint16_t(rgb565_local(255,136,64)),red=uint16_t(rgb565_local(255,96,80));
  // Meshtastic's green, for Mesh's title bar and the row picked; frame for boxes.
  uint16_t accent=uint16_t(rgb565_local(103,234,148)),frame=uint16_t(rgb565_local(52,64,74));
  // Bubbles, navy for what's heard and violet for what's sent, and Help's buttons. Each is picked to
  // survive the canvas's 3-3-2 colour, which turns most dark tints black.
  uint16_t heard=uint16_t(rgb565_local(16,40,90)),sent=uint16_t(rgb565_local(40,10,100));
  uint16_t helpFill=uint16_t(rgb565_local(80,12,16));
  uint16_t ink=0;  // Text on a filled bar.
};
size_t meshGlyphEnd(const String& s,size_t i) {
  size_t e=i+1;
  while(e<s.length() && (uint8_t(s[e])&0xc0)==0x80)++e;
  return e;
}
void meshUseFont(bool ascii,bool big) {
  if(big && ascii)gCanvas.setFont(&fonts::Font2);
  else gCanvas.setFont(&fonts::efontCN_12);
}
// Each stretch of ASCII, or of anything else, with its font already set.
template<class F> void meshRuns(const String& s,bool big,F each) {
  for(size_t i=0;i<s.length();) {
    const bool ascii=uint8_t(s[i])<0x80;
    size_t e=i;
    while(e<s.length() && (uint8_t(s[e])<0x80)==ascii)++e;
    meshUseFont(ascii,big);each(s.substring(i,e),ascii);i=e;
  }
}
int meshWidth(const String& s,bool big=true) {
  int w=0;meshRuns(s,big,[&](const String& run,bool){w+=gCanvas.textWidth(run);});
  return w;
}
void meshText(int x,int y,const String& s,uint16_t color,bool big=true) {
  auto& d=gCanvas;d.setTextColor(color);
  meshRuns(s,big,[&](const String& run,bool ascii){d.setCursor(x,big && !ascii?y+2:y);d.print(run);x+=d.textWidth(run);});
}
// Cut to a width, ending in two dots when it had to be cut.
String meshFit(const String& s,int width,bool big=true) {
  if(meshWidth(s,big)<=width)return s;
  const int room=width-meshWidth(String(".."),big);
  int w=0;size_t i=0;
  while(i<s.length()) {
    const size_t e=meshGlyphEnd(s,i);const int g=meshWidth(s.substring(i,e),big);
    if(w+g>room)break;
    w+=g;i=e;
  }
  return s.substring(0,i)+"..";
}
// The end that fits, for a box being typed into.
String meshTail(const String& s,int width,bool big=true) {
  if(meshWidth(s,big)<=width)return s;
  int w=0;size_t start=s.length();
  while(start>0) {
    size_t g=start-1;
    while(g>0 && (uint8_t(s[g])&0xc0)==0x80)--g;
    const int gw=meshWidth(s.substring(g,start),big);
    if(w+gw>width)break;
    w+=gw;start=g;
  }
  return s.substring(start,s.length());
}
// Font2 when it fits, else the small font, cut if even that's too long.
void meshTextFit(int x,int y,const String& s,int width,uint16_t color) {
  if(meshWidth(s)<=width) {meshText(x,y,s,color);return;}
  meshText(x,y+2,meshFit(s,width,false),color,false);
}
void meshTextRight(int right,int y,const String& s,uint16_t color) {meshText(right-meshWidth(s),y,s,color);}
void meshTextCenter(int y,const String& s,uint16_t color) {
  const String fit=meshFit(s,232);meshText(120-meshWidth(fit)/2,y,fit,color);
}
// The bold font, for titles and what matters most on a screen. ASCII only.
void meshBold(int x,int y,const String& s,uint16_t color,int width=232) {
  auto& d=gCanvas;d.setFont(&fonts::FreeSansBold9pt7b);
  String text=s;
  while(!text.isEmpty() && d.textWidth(text)>width)text.remove(text.length()-1,1);
  d.setTextColor(color);d.setCursor(x,y);d.print(text);
}
int meshBoldWidth(const String& s) {gCanvas.setFont(&fonts::FreeSansBold9pt7b);return gCanvas.textWidth(s);}
void meshBoldCenter(int y,const String& s,uint16_t color) {meshBold(120-std::min(232,meshBoldWidth(s))/2,y,s,color);}
// Bold when it fits, else Font2, else small.
void meshBoldFit(int x,int y,const String& s,int width,uint16_t color) {
  if(meshBoldWidth(s)<=width)meshBold(x,y,s,color,width);
  else meshTextFit(x,y+1,s,width,color);
}
// The bar along the top of every Mesh screen, as the Lab and the menu have it: the app's icon, a
// bold title, a chip on the right for what changes, and a rule in the screen's colour. The title
// keeps its room; the chip gets what's left.
void meshHead(const char* title,uint16_t accent,const String& chip=String(),uint16_t chipColor=0) {
  auto& d=gCanvas;MeshInk ink;
  d.fillScreen(ink.bg);d.setTextSize(1);
  d.drawBitmap(3,1,launcher_icons::kMesh,launcher_icons::kSize,launcher_icons::kSize,accent);
  const int titleW=std::min(238-23,meshBoldWidth(title));
  meshBold(23,1,title,ink.white,titleW);
  const int room=std::min(140,238-(23+titleW+4)-10);
  if(!chip.isEmpty() && room>=24) {
    const String text=meshFit(chip,room);
    const int chipW=meshWidth(text)+10;
    d.drawRoundRect(238-chipW,1,chipW,17,7,chipColor);
    meshText(243-chipW,1,text,chipColor);
  }
  d.drawFastHLine(0,18,240,accent);
}
// Font2 has no arrows, so \x18 \x19 \x1b \x1a (up, down, left, right) are drawn by hand.
void meshArrowGlyph(int x,int y,char kind,uint16_t color) {
  auto& d=gCanvas;
  for(int i=0;i<4;++i) {
    if(kind=='\x18')d.drawFastHLine(x+3-i,y+2+i,i*2+1,color);
    else if(kind=='\x19')d.drawFastHLine(x+3-i,y+5-i,i*2+1,color);
    else if(kind=='\x1b')d.drawFastVLine(x+2+i,y+3-i,i*2+1,color);
    else d.drawFastVLine(x+5-i,y+3-i,i*2+1,color);
  }
}
// The keys, along the bottom, in the small font if Font2 can't fit them.
void meshKeys(const String& text,uint16_t color) {
  int arrows=0;
  for(size_t i=0;i<text.length();++i)if(text[i]>='\x18' && text[i]<='\x1b')++arrows;
  String plain;
  for(size_t i=0;i<text.length();++i)if(text[i]<'\x18' || text[i]>'\x1b')plain+=text[i];
  const bool big=meshWidth(plain)+arrows*9<=228;
  int x=6;String words;
  const auto flush=[&]() {
    if(words.isEmpty())return;
    const String shown=meshFit(words,234-x,big);
    meshText(x,big?kMeshKeysY:kMeshKeysY+2,shown,color,big);x+=meshWidth(shown,big);words="";
  };
  for(size_t i=0;i<text.length();++i) {
    const char c=text[i];
    if(c>='\x18' && c<='\x1b') {flush();meshArrowGlyph(x,kMeshKeysY+4,c,color);x+=9;}
    else words+=c;
  }
  flush();
}
// A notice where the keys go, on its own colour so it isn't missed.
void meshBanner(const String& text,uint16_t fill) {
  MeshInk ink;gCanvas.fillRoundRect(2,117,236,18,6,fill);meshTextFit(8,118,text,224,ink.ink);
}
// A notice at the bottom, while there is one: anything but the steady state lines.
bool meshNoticeShowing() {
  if(gMeshNotice.isEmpty() || gMeshNotice=="Listening / SD paused" || gMeshNotice=="Radio off / SD ready")return false;
  return !gMeshNoticeUntilMs || int32_t(millis()-gMeshNoticeUntilMs)<0;
}
// The bottom line: a notice while there is one, else the keys.
void meshFoot(const String& keys) {
  MeshInk ink;
  if(meshNoticeShowing())meshBanner(gMeshNotice,ink.orange);
  else meshKeys(keys,ink.muted);
}
// --- end of Mesh drawing -------------------------------------------------------------------------

String meshNodeName(uint32_t id) {
  if(gMeshSession) {
    if(const auto* c=gMeshSession->contacts.find(id)) if(c->name[0])return String(c->name);
    for(size_t i=0;i<gMeshSession->state.nodeCount;++i) {
      const auto& n=gMeshSession->state.nodes[i];if(n.id==id && n.name[0])return String(n.name);
    }
  }
  char label[12];snprintf(label,sizeof(label),"!%08lx",static_cast<unsigned long>(id));return String(label);
}
// The private channel by its name, with a # so it can't be taken for a person.
String meshPrivateName() { return String("#")+(gMeshSession?gMeshSession->privateChannel.name:""); }
String meshTargetName(uint8_t target) {
  if(target==s1_mesh::kTargetPrivate)return meshPrivateName();
  const auto* c=gMeshSession?meshTargetContact(*gMeshSession,target):nullptr;
  return c?meshNodeName(c->id):String("Public");
}
// "You > Alex / delivered", or "You / heard nearby" on the public channel.
String meshSentLabel(const s1_mesh::Message& m) {
  String who=m.privateChannel?String("You > ")+meshPrivateName():m.to==s1_mesh::kBroadcast?String("You"):String("You > ")+meshNodeName(m.to);
  String what=s1_mesh::deliveryLabel(m.status,m.direct);
  if(m.status==s1_mesh::Delivery::Failed)what=String("failed: ")+s1_mesh::failureLabel(m.reason);
  return what.isEmpty()?who:who+" / "+what;
}
String meshFromLabel(const s1_mesh::Message& m) {
  const uint32_t me=gMeshSession?gMeshSession->mesh.getNodeNum():0;
  if(m.privateChannel)return meshPrivateName()+" "+meshNodeName(m.from);
  return m.to==me && m.to!=s1_mesh::kBroadcast?meshNodeName(m.from)+" > you":meshNodeName(m.from);
}
String meshClock(uint32_t ms) {
  char text[12];const uint32_t s=(ms+999)/1000;snprintf(text,sizeof(text),"%lu:%02lu",
    static_cast<unsigned long>(s/60),static_cast<unsigned long>(s%60));return String(text);
}
String meshGpsLine(uint32_t now) {
  const auto& w=gMeshSession->where;
  if(s1_mesh::usable(w,now)) {
    const uint32_t age=now-w.atMs;
    if(age<s1_mesh::kFixFreshMs)return String("GPS fix, ")+String(int(w.satellites))+" sats";
    return String("GPS fix from ")+String(int(age/60000))+" min ago";
  }
  return meshGpsHearing(now)?String("GPS: no fix yet, needs open sky"):String("GPS: not heard");
}

// "To: Sam, Jo, #Family, public (rough)": who the next Help round goes to, with how much of the
// location the public channel gets.
String meshHelpTo(const s1_mesh::RadioSession& s) {
  const auto plan=meshHelpPlan(s);
  String to="To: ";bool first=true;
  auto add=[&](const String& who){if(!first)to+=", ";to+=who;first=false;};
  for(size_t i=0;i<s.contacts.count;++i)add(meshNodeName(s.contacts.list[i].id));
  if(plan.privateChannel)add(meshPrivateName());
  if(plan.publicChannel) {
    const auto sharing=s1_mesh::sharingOrDefault(gDeviceSettings.meshPublicLocation);
    add(String("public")+(sharing==s1_mesh::Sharing::Off?" (no location)":sharing==s1_mesh::Sharing::Rough?" (rough)":" (exact)"));
  }
  return to;
}

// A notice shows until its time runs out; one with no time stays until the next.
// "40s ago", "12 min ago", "3 h ago", "2 days ago".
String meshAgo(uint32_t ms) {
  const uint32_t s=ms/1000;
  if(s<60)return String(int(s))+"s ago";
  if(s<3600)return String(int(s/60))+" min ago";
  if(s<86400)return String(int(s/3600))+" h ago";
  return String(int(s/86400))+(s<172800?" day ago":" days ago");
}
uint16_t meshLabelColor(const s1_mesh::Message& m,const MeshInk& ink) {
  if(m.outgoing)return ink.purple;
  if(m.privateChannel)return ink.amber;
  const uint32_t me=gMeshSession?gMeshSession->mesh.getNodeNum():0;
  return m.to==me && m.to!=s1_mesh::kBroadcast?ink.green:ink.cyan;
}
// Word-wraps into lines no wider than width, breaking at a space where one fits, and calls
// line(text, index) for each. Returns how many there are.
template<class F> int meshWrap(const String& text,int width,bool big,F line) {
  String now;int nowW=0,lines=0,space=-1;
  for(size_t i=0;i<text.length();) {
    const size_t e=meshGlyphEnd(text,i);const String glyph=text.substring(i,e);i=e;
    const int w=meshWidth(glyph,big);
    if(nowW+w>width && !now.isEmpty()) {
      const String rest=space>0?now.substring(size_t(space)+1,now.length()):String();
      if(space>0 && meshWidth(rest,big)+w<=width) {line(now.substring(0,size_t(space)),lines++);now=rest;}
      else {line(now,lines++);now=String();}
      nowW=meshWidth(now,big);space=-1;
      if(glyph==" " && now.isEmpty())continue;  // No line starts with a space.
    }
    if(glyph==" ")space=int(now.length());
    now+=glyph;nowW+=w;
  }
  if(!now.isEmpty())line(now,lines++);
  return lines;
}

void renderMeshHelpUi() {
  MeshInk ink;auto& d=gCanvas;auto& s=*gMeshSession;const uint32_t now=millis();
  const String battery=gBatterySnapshot.level>=0?String("Battery ")+String(int(gBatterySnapshot.level))+"%":String("Battery ?");
  meshHead("Help",ink.red,battery,gBatterySnapshot.level>=0 && gBatterySnapshot.level<20?ink.red:ink.muted);
  const bool fix=s1_mesh::usable(s.where,now);
  if(!s.help.on) {
    meshTextFit(8,kMeshTop,meshGpsLine(now),224,fix?ink.green:ink.orange);
    meshTextFit(8,kMeshTop+17,meshHelpTo(s),224,ink.muted);
    // The four messages as buttons, their number on red.
    for(uint8_t c=0;c<s1_mesh::kHelpChoices;++c) {
      const int x=c%2?121:2,y=58+int(c/2)*27;
      const String number(int(c+1));
      d.fillRoundRect(x,y,117,24,6,ink.helpFill);
      d.fillRoundRect(x+4,y+4,16,16,4,ink.red);
      meshText(x+12-meshWidth(number)/2,y+4,number,ink.ink);
      meshText(x+25,y+4,meshFit(s1_mesh::helpLabel(c),88),ink.white);
    }
    meshFoot(s.privateChannel.set()?"1-4 send  P public  C channel":"1-4 send  P public  Esc back");
    return;
  }
  String head=s1_mesh::helpLabel(s.help.choice);
  if(s.help.rounds>1)head+=String(", try ")+String(int(s.help.rounds));
  meshBold(8,kMeshTop,head,ink.white);
  String state;uint16_t stateColor=ink.muted;
  switch(s.help.stop) {
    case s1_mesh::HelpStop::Confirmed: state="Arrived. Stopped repeating";stateColor=ink.green;break;
    case s1_mesh::HelpStop::Replied: state="Someone wrote back. Stopped";stateColor=ink.green;break;
    case s1_mesh::HelpStop::Ok: state="Sent once";break;
    case s1_mesh::HelpStop::None:
      state=s.help.rounds && s.outbox.idle()?String("Again in ")+meshClock(s.help.nextInMs(now)):String("Sending...");
      stateColor=ink.red;break;
  }
  meshText(8,kMeshTop+20,meshFit(state,224),stateColor);
  // While it's still going out, whether the next one carries a position.
  if(s.help.stop==s1_mesh::HelpStop::None) {
    const String gps=fix?"GPS fix":meshGpsHearing(now)?"No GPS fix":"No GPS";
    if(meshWidth(state)+meshWidth(gps)+12<=224)meshTextRight(232,kMeshTop+20,gps,fix?ink.green:ink.orange);
  }
  // A row for each message this round: contacts, then the channels. Four fit; past that, a count.
  size_t total=0,shown=0;
  for(const uint32_t seq:s.help.seq)if(seq)++total;
  int y=58;
  for(size_t i=0;i<s1_mesh::kHelpSlots;++i) {
    if(!s.help.seq[i])continue;
    if(shown==3 && total>4) {meshText(8,y,String("+")+String(int(total-3))+" more",ink.muted);break;}
    const auto* m=s.state.bySeq(s.help.seq[i]);
    const String who=i<s1_mesh::kContacts?meshNodeName(s.contacts.list[i].id):
      i==s1_mesh::kHelpPrivateSlot?meshPrivateName():String("Public");
    String what=m?String(s1_mesh::deliveryLabel(m->status,m->direct)):String("-");
    if(m && m->status==s1_mesh::Delivery::Failed)what=String("failed: ")+s1_mesh::failureLabel(m->reason);
    meshText(8,y,meshFit(who,98),ink.white);
    meshText(112,y,meshFit(what,120),m && m->status==s1_mesh::Delivery::Delivered?ink.green:ink.muted);
    y+=15;++shown;
  }
  meshFoot(!s.help.repeating()?"1-4 send again  Esc back":s.privateChannel.set()?"1-4 change  P public  C channel":
    "1-4 change  P public  Esc back");
}

String meshBand(uint8_t region) {return String(s1_mesh::regionName(region))+" / LongFast";}

// The first screen: what Mesh can do, as a list like the menu's, each row with its own key.
// The radio runs under it, so the band chip is green once it's hearing, the Chat row counts what came
// in while the chat wasn't showing, and Radio Scan's row how many radios it has heard.
void meshRenderStart(uint8_t region) {
  MeshInk ink;auto& d=gCanvas;
  const auto* s=gMeshSession.get();
  const bool hearing=s && s->initResult.load(std::memory_order_acquire)==0;
  meshHead("Mesh",ink.accent,meshBand(region),hearing?ink.accent:ink.muted);
  char longName[s1_mesh::kNameBytes+1],shortName[5];meshSavedName(longName,shortName);
  char chat[16],radios[16]="";
  if(hearing && s->unread)snprintf(chat,sizeof(chat),"%u new",unsigned(s->unread));
  else snprintf(chat,sizeof(chat),"%.3f MHz",double(s1_mesh::regionFrequency(region)));
  if(hearing && s->heard.nodeCount)
    snprintf(radios,sizeof(radios),"%u%s radio%s",unsigned(s->heard.nodeCount),s->heard.moreNodes?"+":"",
             s->heard.nodeCount==1 && !s->heard.moreNodes?"":"s");
  struct Row {const char* label;String value;const char* key;const char* about;};
  const Row rows[kMeshStartRows]={
    {"Chat",String(chat),"","Region: Settings > Device"},
    {"Help",String(),"H","Ready messages with where you are"},
    {"Radio Scan",String(radios),"S","What the radio hears, live"},
    {"Your name",String(longName),"N","What others see on the mesh"},
    {"Private channel",meshChannelLabel(),"C","Only your group can read it"},
  };
  const uint8_t picked=gMeshStartRow%kMeshStartRows;
  for(uint8_t i=0;i<kMeshStartRows;++i) {
    const int y=kMeshTop+int(i)*19;const bool chosen=i==picked;
    if(chosen)d.fillRoundRect(1,y,238,18,6,ink.accent);
    meshText(8,y+1,rows[i].label,chosen?ink.ink:ink.white);
    if(rows[i].key[0])meshTextRight(232,y+1,rows[i].key,chosen?ink.ink:ink.muted);
    if(!rows[i].value.isEmpty()) {
      const int right=rows[i].key[0]?214:232,left=8+meshWidth(rows[i].label)+12;
      const bool news=i==0 && hearing && s->unread;
      meshTextRight(right,y+1,meshFit(rows[i].value,right-left),chosen?ink.ink:news?ink.green:ink.muted);
    }
  }
  meshFoot(rows[picked].about);
}

// While the radio starts, before the chat or Radio Scan can show anything.
void meshRenderStarting(uint8_t region,bool scanning) {
  MeshInk ink;
  meshHead(scanning?"Radio Scan":"Mesh",ink.accent,meshBand(region),ink.muted);
  meshBoldCenter(40,"Starting the radio",ink.white);
  char channel[40];
  snprintf(channel,sizeof(channel),"Public channel, %.3f MHz",double(s1_mesh::regionFrequency(region)));
  meshTextCenter(64,channel,ink.muted);
  if(!gMeshNotice.isEmpty())meshTextCenter(84,gMeshNotice,ink.orange);
  meshKeys("Esc back",ink.muted);
}

void meshRenderName() {
  MeshInk ink;auto& d=gCanvas;
  meshHead("Your name",ink.accent);
  meshText(8,kMeshTop+3,"Others on the mesh see it",ink.muted);
  d.drawRoundRect(2,44,236,22,6,ink.frame);
  if(gMeshNameInput[0])meshText(8,47,meshTail(String(gMeshNameInput),222),ink.white);
  else meshText(8,47,s1_mesh::kDefaultLongName,ink.muted);
  char shortName[5];s1_mesh::shortNameFor(gMeshNameInput[0]?gMeshNameInput:"",shortName);
  if(!gMeshNameInput[0])strcpy(shortName,s1_mesh::kDefaultShortName);
  meshText(8,74,String("Short name: ")+shortName,ink.white);
  meshText(8,92,"Empty is S1-ADV",ink.muted);
  meshKeys("Enter save  Esc cancel",ink.muted);
}

void meshRenderPeople(const s1_mesh::RadioSession& session) {
  MeshInk ink;auto& d=gCanvas;const auto& state=session.state;
  const size_t people=meshPeopleCount(session);
  meshHead("People",ink.accent,String(int(session.contacts.count))+" contacts, "+String(int(state.nodeCount))+" heard",ink.muted);
  if(!people) {
    meshBoldCenter(44,"No radios heard yet",ink.white);
    meshTextCenter(68,"They show up when they send",ink.muted);
  }
  // Three to a page: each with its name, and its number with when it was last heard.
  const size_t first=people?(min(state.nodeSel,people-1)/3)*3:0;
  for(size_t row=0;row<3;++row) {
    MeshPerson p;if(!meshPersonAt(session,first+row,p))break;
    const int y=kMeshTop+int(row)*32;const bool chosen=first+row==state.nodeSel;
    if(chosen)d.fillRoundRect(1,y,238,31,6,ink.accent);
    String tag;
    if(p.contact)tag="contact";
    else if(p.node)tag=String(p.node->rssi)+" dBm";
    const int tagW=tag.isEmpty()?0:meshWidth(tag)+8;
    if(!tag.isEmpty())meshTextRight(232,y+1,tag,chosen?ink.ink:ink.green);
    meshText(8,y+1,meshFit(p.name[0]?String(p.name):meshNodeName(p.id),224-tagW),chosen?ink.ink:ink.white);
    char id[12];snprintf(id,sizeof(id),"!%08lx",static_cast<unsigned long>(p.id));
    String line=String(id)+"  ";
    if(p.node)line+=meshAgo(millis()-p.node->seen)+(p.contact||p.node->hasKey?"":"  no key yet");
    else line+="not heard yet";
    meshText(8,y+16,meshFit(line,224),chosen?ink.ink:ink.muted);
  }
  MeshPerson sel;const bool any=meshPersonAt(session,state.nodeSel,sel);
  meshFoot(any && sel.contact?"\x18\x19  Del remove  Esc back":"\x18\x19  Enter save  Esc back");
}

// Ctrl+O: one message whole, in Font2 if five lines hold it, else in the small font's six.
void meshRenderMessage(const s1_mesh::RadioSession& session) {
  MeshInk ink;const auto& state=session.state;
  const size_t index=state.messageCount-1-state.historyOffset;
  const auto& m=state.messages[index];
  meshHead("Mesh",ink.accent,String(int(index+1))+" of "+String(int(state.messageCount)),ink.muted);
  meshText(8,kMeshTop,meshFit(m.outgoing?meshSentLabel(m):meshFromLabel(m),224),meshLabelColor(m,ink));
  const String text(m.text);
  const auto none=[](const String&,int){};
  const bool big=meshWrap(text,224,true,none)<=5;
  const int lineH=big?16:12,most=big?5:6,total=meshWrap(text,224,big,none);
  meshWrap(text,224,big,[&](const String& line,int n) {
    if(n>=most)return;
    // The last line that fits says when there's more.
    const String shown=n==most-1 && total>most?meshFit(line+String(" ..."),224,big):line;
    meshText(8,38+n*lineH,shown,ink.white,big);
  });
  meshFoot("\x18\x19 older and newer  Esc back");
}

// The chat: the last two messages as bubbles, sent ones to the right, then what's being typed.
void meshRenderChat(const s1_mesh::RadioSession& session) {
  MeshInk ink;auto& d=gCanvas;const auto& state=session.state;
  // Looking back, the chip says where; otherwise it's the band.
  const String chip=state.historyOffset && state.messageCount?String(int(state.messageCount-state.historyOffset))+" of "+
    String(int(state.messageCount)):meshBand(session.region);
  meshHead("Mesh",ink.accent,chip,state.historyOffset?ink.muted:ink.accent);
  if(!state.messageCount) {
    meshBoldCenter(40,"Listening...",ink.white);
    meshTextCenter(62,"No messages yet",ink.muted);
  } else for(size_t row=0;row<2;++row) {
    const int index=int(state.messageCount)-2+int(row)-int(state.historyOffset);if(index<0)continue;
    const auto& m=state.messages[size_t(index)];
    const int y=kMeshTop+int(row)*37,x=m.outgoing?12:2;
    d.fillRoundRect(x,y,226,34,6,m.outgoing?ink.sent:ink.heard);
    meshText(x+6,y+1,meshFit(m.outgoing?meshSentLabel(m):meshFromLabel(m),214),meshLabelColor(m,ink));
    meshText(x+6,y+17,meshFit(String(m.text),214),ink.white);
  }
  // Who Enter sends to, then what's typed. Fn with Left and Right changes who.
  d.drawRoundRect(2,95,236,21,6,ink.frame);
  const String to=meshFit(meshTargetName(state.target),96)+">";
  const uint16_t toColor=state.target==s1_mesh::kTargetPrivate?ink.amber:state.target?ink.green:ink.cyan;
  meshText(8,97,to,toColor);
  const int textX=8+meshWidth(to)+4;
  if(state.input[0])meshText(textX,97,meshTail(String(state.input),232-textX),ink.white);
  else meshText(textX,97,"Message...",ink.muted);
  // A notice first, then Help while it repeats, then two lines of keys taking turns.
  if(meshNoticeShowing())meshBanner(gMeshNotice,ink.orange);
  else if(session.help.repeating())meshBanner(String("Help on, again in ")+meshClock(session.help.nextInMs(millis())),ink.red);
  else meshKeys((millis()/4000)%2?"C-N people  C-O read  C-L location":"Enter send  Fn\x1b\x1a who  C-H help",ink.muted);
}

void renderMeshUi() {
  if(gMeshChannelSetup) {renderMeshChannelUi();return;}
  if(gMeshNaming) {meshRenderName();return;}
  // A running radio keeps the region it started with, so show that one.
  const uint8_t region=gMeshSession?uint8_t(gMeshSession->region):gDeviceSettings.meshRegion;
  if(!gMeshSession || gMeshView==MeshView::Hub) {meshRenderStart(region);return;}
  if(gMeshSession->initResult.load(std::memory_order_acquire)!=0) {meshRenderStarting(region,gMeshView==MeshView::Live);return;}
  auto& session=*gMeshSession;
  if(gMeshView==MeshView::Live) {renderRadioScanUi(session);return;}
  if(session.state.helpView)renderMeshHelpUi();
  else if(session.state.nodeView)meshRenderPeople(session);
  else if(session.state.detailView && session.state.messageCount)meshRenderMessage(session);
  else meshRenderChat(session);
}
} // namespace
