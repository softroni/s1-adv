// Meshtastic leaf mode owns EXT SPI while active. No radio at ordinary boot.
namespace {
bool gMeshNaming = false, gMeshOpenHelp = false;
char gMeshNameInput[s1_mesh::kNameBytes + 1] = {};
// Help is still repeating, so the first Esc only warns: the radio going off would end it.
constexpr uint32_t kMeshStopConfirmMs = 4000;
// Ctrl+L on the public channel asks once more, since everyone hears it.
constexpr uint32_t kMeshPublicLocationMs = 4000;

bool handleMeshEscape() {
  if(gUiMode!=UiMode::Mesh || gLauncherVisible)return false;
  if(gMeshNaming) {gMeshNaming=false;return true;}
  if(gMeshSession) {
    auto& s=*gMeshSession;auto& st=s.state;
    if(st.detailView) {st.detailView=false;return true;}
    if(st.helpView) {st.helpView=false;return true;}
    if(st.nodeView) {st.nodeView=false;return true;}
    if(s.help.repeating() && (!s.stopAskedMs || millis()-s.stopAskedMs>=kMeshStopConfirmMs)) {
      s.stopAskedMs=max<uint32_t>(millis(),1);
      gMeshNotice="Help is on. Esc again ends it";gMeshNoticeUntilMs=millis()+kMeshStopConfirmMs;
      return true;
    }
  }
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

bool meshStartIsBusy() {
  return radioBusy() || gBootBackgroundActive || gRecording || gSubmitting || gThinking || gPlaybackActive ||
    gTransferUiActive || gOtaApplyInProgress || gInboxHttpBusy || gVoiceDialBusy ||
    gVoiceReplySpeechBusy || !gQueuedVoiceReplySpeech.isEmpty() ||
    gRealtimeActive || gRealtimeDialBusy || gRealtimeExclusiveMode || gPendingRealtimeStart ||
    gTextTurnTaskHandle || gCompletedTextTurn || !gPendingTextTurns.empty() ||
    !gActiveTextTurn.isEmpty() || gG0VoicePending || gVoiceCapturePreparing;
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
  if(gMeshSession && gMeshSession->use==s1_mesh::Use::Chat && gMeshSession->historyDirty)meshWriteHistory(gMeshSession->state);
  meshGps(false);
  gMeshSession.reset(); // Clears the IRQ callback and sleeps before SD resumes.
  digitalWrite(kRfidCsPin,HIGH);
  meshRestoreCap();
  gMeshBusOwned=false;gMeshCancelStart=false;gMeshExitAfterStop=false;
  gSdReady=mountSdCardOnSharedSpi();
  gStorageReady=gLittleFsReady || gSdReady;
  gMeshNotice=gSdReady ? "Radio off / SD ready" : "Radio off / SD unavailable";
  logf("[MESH] stopped sd=%d heap=%u",gSdReady,unsigned(ESP.getFreeHeap()));
  return true;
}

void startMeshMode(s1_mesh::Use use) {
  const bool openHelp=gMeshOpenHelp;gMeshOpenHelp=false;
  if(gMeshBusOwned)return;
  if(meshStartIsBusy()) {gMeshNotice="Finish current task first";return;}
  if(!gCanvasSpriteActive) {gMeshNotice="Display buffer recovering";return;}
  if(ESP.getFreeHeap()<65536 || heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)<24576) {
    gMeshNotice="Not enough free RAM";return;
  }
  // Busy/heap failures leave SD and radio untouched.
  gMeshSession.reset(new(std::nothrow) s1_mesh::RadioSession(gSdSpi,gDeviceSettings.meshRegion,use));
  if(!gMeshSession) {gMeshNotice="Not enough free RAM";return;}
  gMeshNotice="Preparing radio...";render();
  // Scan never sends, so it needs no identity and writes nothing to NVS.
  if(use==s1_mesh::Use::Chat && !loadMeshIdentity(*gMeshSession)) {
    gMeshSession.reset();gMeshNotice="Identity storage failed";return;
  }
  if(use==s1_mesh::Use::Chat) {
    // The name it announces, who it can message, and what was said before it last stopped.
    meshLoadProfile(*gMeshSession);
    meshLoadHistory(gMeshSession->state);
    for(size_t i=0;i<gMeshSession->state.messageCount;++i)
      gMeshSession->nextSeq=max<uint32_t>(gMeshSession->nextSeq,gMeshSession->state.messages[i].seq+1);
    gMeshSession->state.helpView=openHelp;
  }
  if(!meshEnableCap()) {
    gMeshSession.reset();gMeshNotice="Cap-1262 not detected";return;
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
    stopMeshMode(false);gMeshNotice="Radio task allocation failed";
  }
}

// The history and Help hear how each message the outbox sent turned out.
struct MeshSink {
  s1_mesh::RadioSession& s;
  void status(uint32_t seq,s1_mesh::Delivery d,uint8_t reason,uint32_t id) {
    if(auto* m=s.state.bySeq(seq)) {if(id)m->id=id;m->status=d;m->reason=reason;s.historyDirty=true;}
    if(d==s1_mesh::Delivery::Delivered) {
      const bool wasOn=s.help.repeating();s.help.delivered(seq);
      if(wasOn && !s.help.repeating()) {meshWakeScreen();meshChime(false);}
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
// Puts a text in the outbox and the history. False when the outbox is full.
bool meshQueueText(s1_mesh::RadioSession& s,uint8_t target,const char* text,uint32_t* seqOut=nullptr) {
  const auto* contact=meshTargetContact(s,target);
  if(target && !contact)return false;
  s1_mesh::Outgoing o;
  o.seq=s.nextSeq;o.to=contact?contact->id:s1_mesh::kBroadcast;o.pki=contact!=nullptr;
  if(contact)memcpy(o.key,contact->key,32);
  o.len=uint8_t(min<size_t>(strlen(text),s1_mesh::kTextBytes));memcpy(o.data,text,o.len);
  if(!s.outbox.push(o))return false;
  s.state.queued(s.mesh.getNodeNum(),o.to,o.pki,o.seq,text);
  ++s.nextSeq;s.historyDirty=true;
  if(seqOut)*seqOut=o.seq;
  return true;
}

// One Help round: the message with where the GPS last had a fix, to every contact, and to the
// public channel as well when asked or when there's nobody else to send it to.
void meshHelpRound(s1_mesh::RadioSession& s,uint32_t now) {
  char text[s1_mesh::kTextBytes+1];
  s1_mesh::composeHelp(text,sizeof(text),s.help.choice,s.where,now);
  s.help.seq={};
  for(size_t i=0;i<s.contacts.count;++i) {uint32_t seq=0;if(meshQueueText(s,uint8_t(i+1),text,&seq))s.help.seq[i]=seq;}
  if(s.help.alsoPublic || !s.contacts.count) {uint32_t seq=0;if(meshQueueText(s,0,text,&seq))s.help.seq[s1_mesh::kContacts]=seq;}
  s.help.roundSent(now);
  logf("[MESH] help round=%u choice=%u contacts=%u public=%d fix=%d",unsigned(s.help.rounds),unsigned(s.help.choice),
    unsigned(s.contacts.count),s.help.seq[s1_mesh::kContacts]!=0,s1_mesh::usable(s.where,now));
}

// What Chat does on every pass once the radio is up.
void meshServiceMessaging(s1_mesh::RadioSession& s,uint32_t now) {
  meshGps(true);
  meshReadGps(now,s.where);
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
      const bool toMe=packet.header.to==me,contact=s.contacts.find(packet.header.from)!=nullptr;
      s.help.replied(contact,toMe,now);
      if(toMe || contact) {meshWakeScreen();meshChime(true);}
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
    if(result)gMeshNotice="Radio init failed: "+String(result);
    if(home)setUiMode(UiMode::Home);
    return;
  }
  auto& session=*gMeshSession;
  session.lowHeap=std::min(session.lowHeap,uint32_t(ESP.getFreeHeap()));
  session.lowLargest=std::min(session.lowLargest,uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
  if(session.use==s1_mesh::Use::Scan) {serviceRadioScan(session);return;}
  if(!session.readyReported) {
    session.readyReported=true;
    logf("[MESH] ready %s LongFast %.3fMHz node=!%08lx tx=%ddBm contacts=%u heap=%u largest=%u",s1_mesh::regionName(session.region),
      double(s1_mesh::regionFrequency(session.region)),static_cast<unsigned long>(session.mesh.getNodeNum()),
      int(s1_mesh::kTxPowerDbm),unsigned(session.contacts.count),unsigned(session.lowHeap),unsigned(session.lowLargest));
  }
  session.mesh.onReceive(meshHandlePacket);
  session.mesh.onReceivePKI(meshKeyFor);
  session.mesh.update();
  meshServiceMessaging(session,millis());
  if(!gMeshNoticeUntilMs || int32_t(millis()-gMeshNoticeUntilMs)>=0)gMeshNotice="Listening / SD paused";
}

bool meshReady() {
  return gMeshSession && gMeshSession->use==s1_mesh::Use::Chat &&
    gMeshSession->initResult.load(std::memory_order_acquire)==0 && !gMeshCancelStart;
}

bool meshSendInput() {
  if(!gMeshSession || gMeshSession->initResult.load(std::memory_order_acquire)!=0 || gMeshCancelStart)return false;
  auto& s=*gMeshSession;
  if(s.use!=s1_mesh::Use::Chat)return false;  // Scan only listens.
  if(!s.state.input[0])return true;
  if(!meshQueueText(s,s.state.target,s.state.input)) {gMeshNotice="Outbox full; wait a moment";gMeshNoticeUntilMs=millis()+2000;return false;}
  s.state.input[0]=0;s.state.historyOffset=0;s.state.detailView=false;s.state.nodeView=false;
  return true;
}

// Ctrl+L: where the GPS has the Cardputer, as a map pin for a Meshtastic app and as text with a
// map link, to whoever Enter sends to. Everyone hears the public channel, so that asks twice.
void meshShareLocation() {
  if(!meshReady())return;
  auto& s=*gMeshSession;const uint32_t now=millis();
  if(!s1_mesh::usable(s.where,now)) {gMeshNotice="No GPS fix yet: needs open sky";gMeshNoticeUntilMs=now+2500;return;}
  const auto* contact=meshTargetContact(s,s.state.target);
  if(!contact && (!s.publicLocationMs || now-s.publicLocationMs>=kMeshPublicLocationMs)) {
    s.publicLocationMs=max<uint32_t>(now,1);
    gMeshNotice="Everyone hears it: C-L again";gMeshNoticeUntilMs=now+kMeshPublicLocationMs;return;
  }
  s.publicLocationMs=0;
  s1_mesh::Outgoing pin;pin.to=contact?contact->id:s1_mesh::kBroadcast;pin.pki=contact!=nullptr;pin.reliable=false;
  pin.port=s1_mesh::kPortPosition;if(contact)memcpy(pin.key,contact->key,32);
  pin.len=uint8_t(s1_mesh::encodePosition(pin.data,sizeof(pin.data),s.where.latE5,s.where.lonE5,s.where.satellites));
  char text[s1_mesh::kTextBytes+1];s1_mesh::composeLocation(text,sizeof(text),s.where,now);
  if(s.outbox.queued()+2>s1_mesh::kOutbox || !s.outbox.push(pin) || !meshQueueText(s,s.state.target,text)) {
    gMeshNotice="Outbox full; wait a moment";gMeshNoticeUntilMs=now+2000;return;
  }
  gMeshNotice="Location queued";gMeshNoticeUntilMs=now+2000;
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
  if(p.contact) {gMeshNotice="Already a contact. Del removes";gMeshNoticeUntilMs=now+2000;return;}
  if(!p.node || !p.node->hasKey) {gMeshNotice="No key heard yet: restart theirs";gMeshNoticeUntilMs=now+2500;return;}
  const auto added=s.contacts.add(p.id,p.node->name[0]?p.node->name:"",p.node->key);
  if(added==s1_mesh::Contacts::Add::Full) {gMeshNotice="Four contacts is the most";gMeshNoticeUntilMs=now+2000;return;}
  gMeshNotice=meshSaveContacts(s.contacts)?"Saved as a contact":"Saved until Mesh closes";gMeshNoticeUntilMs=now+2000;
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
  gMeshNotice="Contact removed";gMeshNoticeUntilMs=millis()+2000;
}

bool meshHelpKey(char key,const Keyboard_Class::KeysState& keys) {
  auto& s=*gMeshSession;
  const char lower=char(tolower(static_cast<unsigned char>(key)));
  if(keys.ctrl && lower=='h') {s.state.helpView=false;return true;}
  if(key>='1' && key<='0'+char(s1_mesh::kHelpChoices)) {
    if(!meshReady()) {gMeshNotice="Radio still starting";gMeshNoticeUntilMs=millis()+1500;return true;}
    s.help.start(uint8_t(key-'1'),millis());s.stopAskedMs=0;
    logf("[MESH] help start choice=%u contacts=%u",unsigned(key-'1'),unsigned(s.contacts.count));
    return true;
  }
  if(lower=='p') {s.help.alsoPublic=!s.help.alsoPublic;return true;}
  return true;
}

bool meshNameKey(char key,const Keyboard_Class::KeysState& keys) {
  if(keys.ctrl)return true;
  if(keys.enter || key=='\r' || key=='\n') {
    char clean[s1_mesh::kNameBytes+1];s1_mesh::cleanName(clean,sizeof(clean),gMeshNameInput);
    gMeshNotice=meshSaveName(clean)?"Name saved":"Couldn't save the name";gMeshNoticeUntilMs=millis()+2000;
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

bool handleMeshKey(char key,const Keyboard_Class::KeysState& keys) {
  if(keys.ctrl && (key=='r'||key=='R'||key=='v'||key=='V')) {setStatus("Esc exits Mesh for voice",1600);return true;}
  const char lower=char(tolower(static_cast<unsigned char>(key)));
  if(keys.ctrl && lower!='n' && lower!='o' && lower!='l' && lower!='h')return false;
  if(gMeshNaming && !gMeshSession)return meshNameKey(key,keys);
  if(!gMeshSession) {
    if(keys.ctrl)return true;
    if(keys.enter || key=='\r' || key=='\n')startMeshMode(s1_mesh::Use::Chat);
    else if(lower=='s')startMeshMode(s1_mesh::Use::Scan);
    else if(lower=='h') {gMeshOpenHelp=true;startMeshMode(s1_mesh::Use::Chat);}
    else if(lower=='n') {
      gMeshNaming=true;
      char shortName[5];meshSavedName(gMeshNameInput,shortName);
      if(!strcmp(gMeshNameInput,s1_mesh::kDefaultLongName))gMeshNameInput[0]=0;
    }
    return true;
  }
  if(gMeshSession->use==s1_mesh::Use::Scan) {
    if(keys.ctrl)return false;
    if(gMeshSession->initResult.load(std::memory_order_acquire)==0)handleRadioScanKey(key,keys);
    return true;
  }
  auto& s=*gMeshSession;auto& state=s.state;
  if(state.helpView)return meshHelpKey(key,keys);
  if(keys.ctrl && lower=='h') {state.helpView=true;state.nodeView=false;state.detailView=false;return true;}
  if(keys.ctrl && lower=='n') {state.nodeView=!state.nodeView;state.detailView=false;return true;}
  if(keys.ctrl && lower=='o') {state.detailView=!state.detailView;state.nodeView=false;return true;}
  if(keys.ctrl && lower=='l') {meshShareLocation();return true;}
  if(key==3 || key==4) {
    const uint8_t choices=uint8_t(s.contacts.count+1);
    state.target=uint8_t((state.target+(key==3?choices-1:1))%choices);
    return true;
  }
  if(key==1 || key==2) {
    if(state.nodeView) {
      const size_t people=meshPeopleCount(s);
      if(people)state.nodeSel=(state.nodeSel+(key==1?people-1:1))%people;
    } else if(state.messageCount) {
      state.historyOffset=(state.historyOffset+(key==1?1:state.messageCount-1))%state.messageCount;
    }
    return true;
  }
  if(state.nodeView && (keys.enter || key=='\r' || key=='\n')) {meshSaveContact();return true;}
  if(state.nodeView && (keys.del || key=='\b')) {meshRemoveContact();return true;}
  if(keys.enter || key=='\r' || key=='\n') {meshSendInput();return true;}
  if(keys.del || key=='\b') {state.erase();return true;}
  if(key>=32 && key<127) {
    const String text=translateInputChar(key);
    state.append(text.c_str());state.detailView=false;state.nodeView=false;
  }
  return true;
}

bool handleMeshHostCommand(const String& line) {
  if(!line.startsWith("mesh."))return false;
  if(line=="mesh.open")setUiMode(UiMode::Mesh);
  else if(line=="mesh.start") {setUiMode(UiMode::Mesh);if(gUiMode==UiMode::Mesh)startMeshMode(s1_mesh::Use::Chat);}
  else if(line=="mesh.scan") {setUiMode(UiMode::Mesh);if(gUiMode==UiMode::Mesh)startMeshMode(s1_mesh::Use::Scan);}
  else if(line=="mesh.stop") {if(stopMeshMode(true))setUiMode(UiMode::Home);}
  else if(line=="mesh.nodes") {if(gMeshSession)gMeshSession->state.nodeView=true;}
  else if(line=="mesh.status") {
    logf("[MESH] region=%s owned=%d init=%d sd=%d heap=%u largest=%u nodes=%u messages=%u",
      s1_mesh::regionName(gMeshSession?uint8_t(gMeshSession->region):gDeviceSettings.meshRegion),gMeshBusOwned,
      gMeshSession?gMeshSession->initResult.load():s1_mesh::kInitPending,gSdReady,unsigned(ESP.getFreeHeap()),
      unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)),
      unsigned(gMeshSession?gMeshSession->state.nodeCount:0),unsigned(gMeshSession?gMeshSession->state.messageCount:0));
    if(gMeshSession && gMeshSession->use==s1_mesh::Use::Chat)
      logf("[MESH] contacts=%u outbox=%u busy=%d help=%d round=%u stop=%u fix=%d",unsigned(gMeshSession->contacts.count),
        unsigned(gMeshSession->outbox.queued()),gMeshSession->outbox.busy(),gMeshSession->help.on,unsigned(gMeshSession->help.rounds),
        unsigned(gMeshSession->help.stop),s1_mesh::usable(gMeshSession->where,millis()));
    if(gMeshSession && gMeshSession->use==s1_mesh::Use::Scan)logRadioScanStatus(*gMeshSession);
  }
  return true;
}

String meshClip(const String& source,int width) {
  String text=source;
  while(!text.isEmpty() && gCanvas.textWidth(text)>width)removeLastUtf8Char(text);
  return text;
}
String meshNodeName(uint32_t id) {
  if(gMeshSession) {
    if(const auto* c=gMeshSession->contacts.find(id)) if(c->name[0])return String(c->name);
    for(size_t i=0;i<gMeshSession->state.nodeCount;++i) {
      const auto& n=gMeshSession->state.nodes[i];if(n.id==id && n.name[0])return String(n.name);
    }
  }
  char label[12];snprintf(label,sizeof(label),"!%08lx",static_cast<unsigned long>(id));return String(label);
}
String meshTargetName(uint8_t target) {
  const auto* c=gMeshSession?meshTargetContact(*gMeshSession,target):nullptr;
  return c?meshNodeName(c->id):String("Public");
}
// "You > Alex / delivered", or "You / heard nearby" on the public channel.
String meshSentLabel(const s1_mesh::Message& m) {
  String who=m.to==s1_mesh::kBroadcast?String("You"):String("You > ")+meshNodeName(m.to);
  String what=s1_mesh::deliveryLabel(m.status,m.direct);
  if(m.status==s1_mesh::Delivery::Failed)what=String("failed: ")+s1_mesh::failureLabel(m.reason);
  return what.isEmpty()?who:who+" / "+what;
}
String meshFromLabel(const s1_mesh::Message& m) {
  const uint32_t me=gMeshSession?gMeshSession->mesh.getNodeNum():0;
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

void renderMeshHelpUi() {
  auto& d=gCanvas;auto& s=*gMeshSession;const uint32_t now=millis();
  const uint16_t bg=rgb565_local(3,8,14),muted=rgb565_local(130,145,155),cyan=rgb565_local(47,227,255);
  const uint16_t red=rgb565_local(255,96,80),green=rgb565_local(88,240,141);
  d.setFont(&fonts::Font0);d.setTextColor(s1_mesh::usable(s.where,now)?green:rgb565_local(255,136,64),bg);
  d.setCursor(8,44);d.print(meshClip(meshGpsLine(now),224));
  if(!s.help.on) {
    String to="To: ";
    if(!s.contacts.count)to+="public channel (no contacts)";
    else for(size_t i=0;i<s.contacts.count;++i) {if(i)to+=", ";to+=meshNodeName(s.contacts.list[i].id);}
    if(s.contacts.count && s.help.alsoPublic)to+=", public";
    d.setTextColor(muted,bg);d.setCursor(8,56);d.print(meshClip(to,224));
    d.setFont(&fonts::Font2);d.setTextColor(TFT_WHITE,bg);
    for(uint8_t c=0;c<s1_mesh::kHelpChoices;++c) {
      d.setCursor(c%2?124:8,70+int(c/2)*18);d.print(String(int(c+1))+" ");d.print(meshClip(s1_mesh::helpLabel(c),96));
    }
    d.setFont(&fonts::Font0);d.setTextColor(cyan,bg);d.setCursor(8,127);
    d.print(s.help.alsoPublic?"1-4 send  P public: on  Esc chat":"1-4 send  P public: off  Esc chat");return;
  }
  String head=s1_mesh::helpLabel(s.help.choice);
  if(s.help.rounds>1)head+=String(", try ")+String(int(s.help.rounds));
  d.setFont(&fonts::Font2);d.setTextColor(TFT_WHITE,bg);d.setCursor(8,55);d.print(meshClip(head,224));
  d.setFont(&fonts::Font0);
  String state;uint16_t stateColor=muted;
  switch(s.help.stop) {
    case s1_mesh::HelpStop::Confirmed: state="Arrived. Stopped repeating";stateColor=green;break;
    case s1_mesh::HelpStop::Replied: state="Someone wrote back. Stopped";stateColor=green;break;
    case s1_mesh::HelpStop::Ok: state="Sent once";break;
    case s1_mesh::HelpStop::None:
      state=s.help.rounds && s.outbox.idle()?String("Again in ")+meshClock(s.help.nextInMs(now)):String("Sending...");
      stateColor=red;break;
  }
  d.setTextColor(stateColor,bg);d.setCursor(8,73);d.print(meshClip(state,224));
  int y=85;
  for(size_t i=0;i<=s1_mesh::kContacts && y<=115;++i) {
    if(!s.help.seq[i])continue;
    const auto* m=s.state.bySeq(s.help.seq[i]);
    const String who=i<s1_mesh::kContacts?meshNodeName(s.contacts.list[i].id):String("Public");
    String what=m?String(s1_mesh::deliveryLabel(m->status,m->direct)):String("-");
    if(m && m->status==s1_mesh::Delivery::Failed)what=String("failed: ")+s1_mesh::failureLabel(m->reason);
    d.setTextColor(TFT_WHITE,bg);d.setCursor(8,y);d.print(meshClip(who,96));
    d.setTextColor(m && m->status==s1_mesh::Delivery::Delivered?green:muted,bg);
    d.setCursor(108,y);d.print(meshClip(what,124));y+=10;
  }
  d.setTextColor(cyan,bg);d.setCursor(8,127);
  d.print(s.help.repeating()?"1-4 change  P public  Esc chat":"1-4 send again  Esc chat");
}

void renderMeshUi() {
  auto& d=gCanvas;
  const uint16_t bg=rgb565_local(3,8,14),muted=rgb565_local(130,145,155),cyan=rgb565_local(47,227,255);
  const bool helpScreen=gMeshSession && gMeshSession->use==s1_mesh::Use::Chat && gMeshSession->state.helpView &&
    gMeshSession->initResult.load(std::memory_order_acquire)==0;
  d.fillScreen(bg);d.setTextSize(1);d.setFont(&fonts::Font2);
  d.setTextColor(helpScreen?rgb565_local(255,96,80):TFT_WHITE,bg);d.setCursor(8,5);d.print(helpScreen?"Help":"Mesh");
  // A running radio keeps the region it started with, so show that one.
  const uint8_t region=gMeshSession?uint8_t(gMeshSession->region):gDeviceSettings.meshRegion;
  String band=String(s1_mesh::regionName(region))+" / LongFast";
  if(helpScreen)band=gBatterySnapshot.level>=0?String("Battery ")+String(int(gBatterySnapshot.level))+"%":String("Battery ?");
  d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(232-d.textWidth(band),12);d.print(band);
  d.drawFastHLine(6,23,228,rgb565_local(48,54,61));
  String notice=gMeshNotice.isEmpty()?String(gSdReady?"Radio off / SD ready":"Radio off / SD unavailable"):gMeshNotice;
  uint16_t noticeColor=gMeshBusOwned?rgb565_local(255,136,64):muted;
  // While Help repeats, the chat says so where it says the radio is listening.
  if(gMeshSession && !helpScreen && gMeshSession->help.repeating() && notice=="Listening / SD paused") {
    notice=String("Help on, again in ")+meshClock(gMeshSession->help.nextInMs(millis()));noticeColor=rgb565_local(255,96,80);
  }
  d.setTextColor(noticeColor,bg);d.setCursor(8,29);d.print(meshClip(notice,224));
  const bool scanning=gMeshSession && gMeshSession->use==s1_mesh::Use::Scan;
  if(scanning && gMeshSession->initResult.load(std::memory_order_acquire)==0) {renderRadioScanUi(*gMeshSession);return;}
  if(helpScreen) {renderMeshHelpUi();return;}
  if(!gMeshSession || gMeshSession->initResult.load(std::memory_order_acquire)!=0) {
    if(gMeshNaming && !gMeshSession) {
      d.setFont(&fonts::Font2);d.setTextColor(TFT_WHITE,bg);d.setCursor(8,46);d.print("Your name on the mesh");
      d.drawRect(5,70,230,17,rgb565_local(52,64,74));d.setFont(&fonts::efontCN_12);d.setCursor(10,72);
      String typed=gMeshNameInput[0]?String(gMeshNameInput):String("> S1-ADV");
      while(!typed.isEmpty() && d.textWidth(typed)>220) {size_t n=1;while(n<typed.length() && (uint8_t(typed[n])&0xc0)==0x80)++n;typed.remove(0,n);}
      d.print(typed);
      char shortName[5];s1_mesh::shortNameFor(gMeshNameInput[0]?gMeshNameInput:"",shortName);
      if(!gMeshNameInput[0])strcpy(shortName,s1_mesh::kDefaultShortName);
      d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(8,94);d.print(String("Short name: ")+shortName);
      d.setCursor(8,106);d.print("Others see it. Empty is S1-ADV");
      d.setTextColor(cyan,bg);d.setCursor(8,127);d.print("Enter save  Esc cancel");return;
    }
    d.setFont(&fonts::Font2);d.setTextColor(TFT_WHITE,bg);
    const char* title=scanning?"Radio Scan":"Meshtastic leaf";d.setCursor(120-d.textWidth(title)/2,52);d.print(title);
    char channel[40];
    if(scanning)snprintf(channel,sizeof(channel),"Listening only, nothing sent");
    else snprintf(channel,sizeof(channel),"Public channel, %.3f MHz",double(s1_mesh::regionFrequency(region)));
    d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(120-d.textWidth(channel)/2,74);d.print(channel);
    if(!gMeshBusOwned) {
      char longName[s1_mesh::kNameBytes+1],shortName[5];meshSavedName(longName,shortName);
      const String name=meshClip(String("Name: ")+longName,224);
      d.setCursor(120-d.textWidth(name)/2,88);d.print(name);
      const String where="Region: Settings > Device";
      d.setCursor(120-d.textWidth(where)/2,100);d.print(where);
    }
    d.setTextColor(cyan,bg);d.setCursor(8,127);d.print(gMeshBusOwned?"Starting...  Esc cancel":"Enter chat H help S scan N name");return;
  }
  const auto& session=*gMeshSession;
  const auto& state=session.state;
  if(state.nodeView) {
    const size_t people=meshPeopleCount(session);
    d.setTextColor(cyan,bg);d.setCursor(8,44);d.print("PEOPLE: ");d.print(int(session.contacts.count));d.print(" contacts, ");
    d.print(int(state.nodeCount));d.print(" heard");
    const size_t first=people?(min(state.nodeSel,people-1)/2)*2:0;
    for(size_t row=0;row<2;++row) {
      MeshPerson p;if(!meshPersonAt(session,first+row,p))break;
      const int y=56+int(row)*33;const bool chosen=first+row==state.nodeSel;
      if(chosen)d.drawRect(4,y-2,232,31,rgb565_local(52,64,74));
      d.setFont(&fonts::efontCN_12);d.setTextColor(TFT_WHITE,bg);d.setCursor(8,y);
      d.print(meshClip(p.name[0]?String(p.name):meshNodeName(p.id),150));
      d.setFont(&fonts::Font0);
      if(p.contact) {d.setTextColor(rgb565_local(88,240,141),bg);d.setCursor(232-d.textWidth("contact")-2,y+2);d.print("contact");}
      else if(p.node) {const String rssi=String(p.node->rssi)+" dBm";d.setTextColor(rgb565_local(88,240,141),bg);d.setCursor(232-min<int>(56,d.textWidth(rssi))-2,y+2);d.print(meshClip(rssi,56));}
      d.setTextColor(muted,bg);d.setCursor(8,y+16);
      char line[48];
      if(p.node)snprintf(line,sizeof(line),"!%08lx / %lus%s",static_cast<unsigned long>(p.id),
        static_cast<unsigned long>((millis()-p.node->seen)/1000),p.contact||p.node->hasKey?"":" / no key yet");
      else snprintf(line,sizeof(line),"!%08lx / not heard yet",static_cast<unsigned long>(p.id));
      d.print(meshClip(line,220));
    }
    if(!people) {d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(8,64);d.print("No radios heard yet");}
    d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(8,127);
    MeshPerson sel;const bool any=meshPersonAt(session,state.nodeSel,sel);
    d.print(any && sel.contact?"Up/Down  Del remove  Esc back":"Up/Down  Enter save  Esc back");return;
  }
  if(state.detailView && state.messageCount) {
    const auto& m=state.messages[state.messageCount-1-state.historyOffset];
    d.setTextColor(cyan,bg);d.setCursor(8,42);d.print(meshClip(m.outgoing?meshSentLabel(m):meshFromLabel(m),224));
    d.setFont(&fonts::efontCN_12);d.setTextColor(TFT_WHITE,bg);d.setClipRect(8,54,224,72);
    String line;line.reserve(s1_mesh::kTextBytes);String text(m.text);int y=54;
    for(size_t i=0;i<text.length();) {
      size_t end=i+1;while(end<text.length() && (uint8_t(text[end])&0xc0)==0x80)++end;
      const String glyph=text.substring(i,end);
      if(!line.isEmpty() && d.textWidth(line+glyph)>224) {d.setCursor(8,y);d.print(line);y+=12;line=glyph;} else line+=glyph;
      i=end;
    }
    d.setCursor(8,y);d.print(line);d.clearClipRect();
    d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(8,127);d.print("Up/Down message C-O back");return;
  }
  if(!state.messageCount) {
    d.setFont(&fonts::Font2);d.setTextColor(TFT_WHITE,bg);d.setCursor(56,59);d.print("Listening...");
    d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(66,82);d.print("No messages yet");
  } else for(size_t row=0;row<2;++row) {
    const int index=int(state.messageCount)-2+int(row)-int(state.historyOffset);if(index<0)continue;
    const auto& m=state.messages[size_t(index)];const int y=44+int(row)*31;
    d.setFont(&fonts::Font0);d.setTextColor(m.outgoing?rgb565_local(157,102,255):cyan,bg);d.setCursor(8,y);
    d.print(meshClip(m.outgoing?meshSentLabel(m):meshFromLabel(m),224));
    d.setFont(&fonts::efontCN_12);d.setTextColor(TFT_WHITE,bg);d.setCursor(8,y+10);d.print(meshClip(String(m.text),224));
  }
  d.drawRect(5,108,230,17,rgb565_local(52,64,74));
  // Who Enter sends to, then what's typed. Left and Right change who.
  const String to=meshClip(meshTargetName(state.target),72)+">";
  d.setFont(&fonts::Font0);d.setTextColor(state.target?rgb565_local(88,240,141):cyan,bg);d.setCursor(9,113);d.print(to);
  const int textX=9+d.textWidth(to)+3,textW=232-textX-2;
  d.setFont(&fonts::efontCN_12);d.setTextColor(TFT_WHITE,bg);d.setCursor(textX,110);
  String composer=state.input[0]?String(state.input):String("Message...");
  while(!composer.isEmpty() && d.textWidth(composer)>textW) {
    size_t n=1;while(n<composer.length() && (uint8_t(composer[n])&0xc0)==0x80)++n;composer.remove(0,n);
  }
  d.print(composer);
  // Two lines of keys, taking turns, since one line can't hold them all.
  d.setFont(&fonts::Font0);d.setTextColor(muted,bg);d.setCursor(8,127);
  d.print((millis()/4000)%2?"C-N people C-O read C-L location":"Enter send  <> who  C-H help");
}
} // namespace
