#!/usr/bin/env python3
"""Drones: Remote ID (ASTM F3411) as the Cardputer hears it over Wi-Fi. Frames are built here in
Python from the message layouts (beacons with ASD-STAN's vendor element, and Wi-Fi Aware service
discovery frames), and drone_id.h must read back exactly what went in: who, where, how high and fast,
where it's flown from, and the values that mean "unknown". Broken frames are refused, the table
of drones heard merges and forgets, all under ASan/UBSan. And the page is wired so listening holds
Wi-Fi away from the reconnects and always gives it back."""
from pathlib import Path
import os, struct, subprocess, tempfile
root = Path(__file__).resolve().parents[1]
src = root / 'bots/CardputerADV/src'
parts = src / 'main_parts'
part = lambda name: (parts / name).read_text()
app = part('096_main.cpp.inc')

# --- Wiring -------------------------------------------------------------------------------------
assert 'bool planesHoldsWifi();' in part('005_main.cpp.inc')
conn = part('043_main.cpp.inc').split('void maintainConnectivity() {', 1)[1].split('\n}\n', 1)[0]
assert conn.index('if (planesHoldsWifi()) return;') < conn.index('serviceWifiRoam(now)'), 'no roaming or reconnects while it listens'
http = part('040_main.cpp.inc').split('bool ensureWifiForHttp(', 1)[1].split('\n}\n', 1)[0]
assert http.index('if (planesHoldsWifi()) return false;') < http.index('beginWifiStationConnect'), 'no request dials Wi-Fi while it listens'
assert 'PlView::Drones};' in app and 'kPages[page % 5]' in app, 'Drones is the fifth page'
stop = app.split('void stopPlanes() {', 1)[1].split('\n}\n', 1)[0]
assert stop.strip().startswith('plDroneStop();'), 'leaving the app gives Wi-Fi back first'
esc = app.split('bool planesEscape() {', 1)[1].split('\n}\n', 1)[0]
assert 'case PlView::Drones: plDroneStop(); gPlView = PlView::Sky; break;' in esc
service = app.split('void plDroneService(uint32_t now) {', 1)[1].split('\n}\n', 1)[0]
assert service.strip().startswith('if (gPlDroneState != PlDroneState::Off && !plDroneView()) { plDroneStop(); return; }'), \
    'any other page, the reminder included, stops listening'
give = app.split('void plDroneGiveBack() {', 1)[1].split('\n}\n', 1)[0]
assert 'beginWifiStationConnect(' in give and 'gPlDroneHold = false;' in give
take = app.split('void plDroneTake() {', 1)[1].split('\n}\n', 1)[0]
assert take.index('camera_net::busy()') < take.index('WiFi.disconnect(false, false);'), 'a request under way ends before Wi-Fi goes'
assert 'if (gPlDroneState != PlDroneState::Off || !wifiLocalAddress()) return false;' in app, 'no requests while it listens'
listen = (src / 'drone_listen.cpp').read_text()
assert 'WIFI_PROMIS_FILTER_MASK_MGMT' in listen and 'drone_id::looksLikeRemoteId(data, len)' in listen
assert 'heap_caps_free(old);' in listen.split('void stop() {', 1)[1].split('\n}\n', 1)[0]

# --- Frames, from the layouts -----------------------------------------------------------------------
def header(kind, version=2): return bytes([(kind << 4) | version])
def pad(b, n): return (b + bytes(n))[:n]
def alt(m): return struct.pack('<H', 0 if m is None else round((m + 1000) / 0.5))
def basic_id(id_type, ua_type, ident):
    return header(0) + bytes([(id_type << 4) | ua_type]) + pad(ident.encode(), 20) + bytes(3)
def location(status=2, over_ground=False, direction=225, speed=12.5, climb=2.5, lat=41.9786, lon=-87.9048,
             baro=240.0, geo=250.0, height=61.0, speed_invalid=False, climb_invalid=False):
    if direction is None: direction = 361
    ew = 1 if direction >= 180 else 0
    d = direction - 180 if ew else direction
    if speed_invalid: mult, s = 1, 255
    elif speed <= 63.75: mult, s = 0, round(speed / 0.25)
    else: mult, s = 1, min(254, round((speed - 63.75) / 0.75))
    v = 126 if climb_invalid else round(climb / 0.5)
    flags = (status << 4) | ((1 if over_ground else 0) << 2) | (ew << 1) | mult
    la = 0 if lat is None else round(lat * 1e7)
    lo = 0 if lon is None else round(lon * 1e7)
    body = bytes([flags, d, s]) + struct.pack('<b', v) + struct.pack('<ii', la, lo) + alt(baro) + alt(geo) + alt(height)
    return header(1) + body + bytes([0x3B, 0x24]) + struct.pack('<H', 1234) + bytes([1, 0])
def self_id(text): return header(3) + bytes([0]) + pad(text.encode(), 23)
def system(op_lat, op_lon, kind=1):
    return header(4) + bytes([kind]) + struct.pack('<ii', round(op_lat * 1e7), round(op_lon * 1e7)) + struct.pack('<HBHH', 1, 0, 0, 0) + \
        bytes([0]) + alt(200.0) + struct.pack('<I', 200000000) + bytes(1)
def operator_id(text): return header(5) + bytes([0]) + pad(text.encode(), 20) + bytes(3)
def auth(): return header(2) + bytes(24)
def pack(*messages):
    for m in messages: assert len(m) == 25, len(m)
    return header(15) + bytes([25, len(messages)]) + b''.join(messages)
def mgmt(subtype, da, sa, bssid): return bytes([subtype, 0, 0, 0]) + da + sa + bssid + bytes(2)
def beacon(mac, ssid, p, counter=7, extra_ies=b'', rid=True):
    ies = bytes([0, len(ssid)]) + ssid + bytes([1, 1, 0x8C]) + extra_ies
    vendor = bytes([0xFA, 0x0B, 0xBC, 0x0D, counter]) + p
    body = ies + (bytes([221, len(vendor)]) + vendor if rid else b'')
    return mgmt(0x80, b'\xff' * 6, mac, mac) + bytes(8) + struct.pack('<HH', 100, 0x0421) + body
def nan(mac, p, counter=3, service=bytes([0x88, 0x69, 0x19, 0x9D, 0x92, 0x09])):
    info = bytes([counter]) + p
    sda = bytes([0x03]) + struct.pack('<H', 10 + len(info)) + service + bytes([1, 0, 0x10, len(info)]) + info
    sdea = bytes([0x0E]) + struct.pack('<H', 4) + bytes([1]) + struct.pack('<H', 0x0200) + bytes([counter])
    return mgmt(0xD0, bytes([0x51, 0x6F, 0x9A, 0x01, 0, 0]), mac, bytes([0x50, 0x6F, 0x9A, 0x01, 0, 0])) + \
        bytes([0x04, 0x09, 0x50, 0x6F, 0x9A, 0x13]) + sda + sdea
def c_array(name, b): return 'static const uint8_t %s[] = {%s};' % (name, ','.join(str(x) for x in b))

mac1 = bytes([0x60, 0x60, 0x1F, 0xAA, 0xBB, 0xCC])
mac2 = bytes([0x02, 0x11, 0x22, 0x33, 0x44, 0x55])
full = pack(basic_id(1, 2, '1581F5FKD23A0012'), location(), auth(), self_id('Recreational'), system(41.9775, -87.9070),
            operator_id('FIN87astrdge12k8'))
frames = {
    'fBeacon': beacon(mac1, b'RID-1581F5FKD23A0012', full),
    'fBeaconOtherIe': beacon(mac1, b'RID-x', full, extra_ies=bytes([221, 4, 0x00, 0x50, 0xF2, 0x02])),
    'fNan': nan(mac2, pack(basic_id(4, 4, 'SESSION-7Q2'), location(status=2, over_ground=True, direction=10, speed=100.0, climb=-3.0,
                                                                   height=95.5))),
    'fUnknowns': beacon(mac2, b'RID-u', pack(basic_id(1, 0, ''), location(status=1, direction=None, lat=None, lon=None, baro=None, geo=None,
                                                                         height=None, speed_invalid=True, climb_invalid=True))),
    'fLone': beacon(mac1, b'RID-l', location(direction=90, speed=4.0)),  # One message, no pack.
    'fNotRid': beacon(mac1, b'Home', b'', extra_ies=bytes([221, 6, 0x00, 0x50, 0xF2, 0x02, 1, 2]), rid=False),
    'fWrongService': nan(mac2, pack(location()), service=bytes(6)),
    'fOtherAction': mgmt(0xD0, b'\xff' * 6, mac2, mac2) + bytes([0x04, 0x09, 0x50, 0x6F, 0x9A, 0x12]) + bytes(40),
}
broken = beacon(mac1, b'RID-b', full)
frames['fTruncated'] = broken[:-30]                                     # The element says more than is there.
lying = bytearray(beacon(mac1, b'RID-p', pack(location(), location())))
lying[lying.index(bytes([0xF2, 25, 2])) + 2] = 9                        # A pack that claims nine.
frames['fPackLies'] = bytes(lying)
short_nan = bytearray(nan(mac2, pack(location())))
short_nan[24 + 6 + 3 + 9] = 200                                          # Service info longer than the frame.
frames['fNanLies'] = bytes(short_nan)
arrays = '\n'.join(c_array(k, v) for k, v in frames.items())

code = r'''
#include "drone_id.h"
#include <assert.h>
#include <stdio.h>
#include <string>
using namespace drone_id;
@ARRAYS@
static std::string s(const char* t) { return t; }
static bool read(const uint8_t* f, size_t n, Drone& d, Via& via) {
  uint8_t mac[6]; const uint8_t* p = nullptr; size_t len = 0;
  if (!looksLikeRemoteId(f, n) || !findPack(f, n, mac, p, len, via)) return false;
  memcpy(d.mac, mac, 6);
  return decodePack(p, len, d) > 0;
}
int main() {
  Drone d; Via via;
  assert(read(fBeacon, sizeof(fBeacon), d, via) && via == Via::Beacon);
  assert(d.mac[0] == 0x60 && d.mac[5] == 0xCC);
  assert(s(d.id) == "1581F5FKD23A0012" && d.idType == 1 && d.uaType == 2);
  assert(d.located && d.latE7 == 419786000 && d.lonE7 == -879048000 && d.status == 2 && !d.heightOverGround);
  assert(d.direction == 225 && d.speedCms == 1250 && d.climbCms == 250);
  assert(d.altBaroDm == 2400 && d.altGeoDm == 2500 && d.heightDm == 610);
  assert(s(d.selfId) == "Recreational" && s(d.operatorId) == "FIN87astrdge12k8");
  assert(d.hasOperator && d.operatorType == 1 && d.opLatE7 == 419775000 && d.opLonE7 == -879070000);
  int32_t m = 0;
  assert(heightMeters(d, m) && m == 61);
  Drone other; assert(read(fBeaconOtherIe, sizeof(fBeaconOtherIe), other, via) && s(other.id) == "1581F5FKD23A0012");

  Drone n; assert(read(fNan, sizeof(fNan), n, via) && via == Via::Nan);
  assert(n.mac[0] == 0x02 && s(n.id) == "SESSION-7Q2" && n.idType == 4 && n.uaType == 4);
  assert(n.direction == 10 && n.speedCms == 48 * 75 + 255 * 25 && n.climbCms == -300 && n.heightOverGround && n.heightDm == 955);
  assert(!n.hasOperator && !n.selfId[0]);

  Drone u; assert(read(fUnknowns, sizeof(fUnknowns), u, via));
  assert(!u.id[0] && !u.located && u.status == 1 && u.direction == -1 && u.speedCms == -1 && u.climbCms == kUnknown);
  assert(u.heightDm == kUnknown && u.altGeoDm == kUnknown && u.altBaroDm == kUnknown && !heightMeters(u, m));

  Drone lone; assert(read(fLone, sizeof(fLone), lone, via) && lone.located && lone.direction == 90 && lone.speedCms == 400);

  Drone none;
  assert(!looksLikeRemoteId(fNotRid, sizeof(fNotRid)) && !read(fNotRid, sizeof(fNotRid), none, via));
  assert(!read(fWrongService, sizeof(fWrongService), none, via));
  assert(!looksLikeRemoteId(fOtherAction, sizeof(fOtherAction)));
  assert(!read(fTruncated, sizeof(fTruncated), none, via));
  assert(!read(fPackLies, sizeof(fPackLies), none, via));
  assert(!read(fNanLies, sizeof(fNanLies), none, via));
  assert(!read(fBeacon, 30, none, via));
  for (size_t cut = 0; cut < sizeof(fBeacon); ++cut) { Drone x; Via v; read(fBeacon, cut, x, v); }  // Never reads past the end.
  for (size_t cut = 0; cut < sizeof(fNan); ++cut) { Drone x; Via v; read(fNan, cut, x, v); }

  // Two Basic IDs: the serial number stays, a session ID doesn't replace it.
  Drone two;
  uint8_t session[25] = {0x02, 0x42}; memcpy(session + 2, "SESSION", 7);
  memcpy(two.id, "1581AAAA", 9); two.idType = 1;
  assert(decodeMessage(session, two) && s(two.id) == "1581AAAA" && two.uaType == 2);

  // --- The table of drones heard ---
  Table* t = new Table();
  uint8_t mac[6]; const uint8_t* p; size_t len;
  assert(findPack(fBeacon, sizeof(fBeacon), mac, p, len, via));
  assert(t->hear(mac, -58, 6, via, p, len, 1000) == 0 && t->count == 1);
  assert(t->hear(mac, -61, 6, via, p, len, 2000) == 0 && t->count == 1 && t->heard[0].frames == 2 && t->heard[0].rssi == -61);
  uint8_t moved[6] = {0x66, 1, 2, 3, 4, 5};  // The same drone with a new address: the same row.
  assert(t->hear(moved, -70, 1, via, p, len, 3000) == 0 && t->count == 1 && t->heard[0].d.mac[0] == 0x66 && t->heard[0].channel == 1);
  assert(findPack(fNan, sizeof(fNan), mac, p, len, via));
  assert(t->hear(mac, -80, 6, via, p, len, 3500) == 1 && t->count == 2 && t->heard[1].via == Via::Nan);
  assert(t->hear(mac, -80, 6, via, p, 10, 3600) == -1);  // Nothing readable: no change.
  for (int i = 0; i < 9; ++i) {                          // More than fit: the oldest goes.
    uint8_t m2[6] = {0x0A, 0, 0, 0, 0, uint8_t(i)};
    uint8_t solo[25] = {0x12, 0x20}; solo[4] = 0;
    assert(t->hear(m2, -90, 6, Via::Beacon, solo, 25, 10000 + i) >= 0);
  }
  assert(t->count == Table::kMax && t->byMac(moved) < 0);
  t->forget(10001 + Table::kForgetMs);  // The oldest left was heard at 10001: just kept.
  assert(t->count == Table::kMax);
  t->forget(10000 + 8 + Table::kForgetMs + 1);
  assert(t->count == 0);
  delete t;

  // --- Words ---
  char name[24];
  shortName(name, sizeof(name), d); assert(s(name) == "DJI #0012");
  shortName(name, sizeof(name), n); assert(s(name) == "Drone #-7Q2");
  Drone anon; anon.mac[4] = 0xAB; anon.mac[5] = 0x0C;
  shortName(name, sizeof(name), anon); assert(s(name) == "Drone #AB0C");
  Drone notDji = n; strcpy(notDji.id, "1581XYZ"); assert(s(maker(notDji)) == "");  // A session ID isn't a serial.
  assert(s(typeName(2)) == "Multirotor drone" && s(typeName(4)) == "VTOL drone" && s(typeName(0)) == "Drone" && s(typeName(99)) == "Drone");
  assert(s(statusWord(2)) == "flying" && s(statusWord(3)) == "EMERGENCY" && s(statusWord(0)) == "");
  assert(s(signalWord(-55)) == "strong" && s(signalWord(-70)) == "good" && s(signalWord(-88)) == "weak");
  puts("drone_id: ok");
}
'''.replace('@ARRAYS@', arrays)

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'test.cpp').write_text(code)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', '-no-pie', '-I' + str(src), str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
print('PASS Drones: Remote ID beacons and Wi-Fi Aware frames read back exactly, unknowns, broken frames, the table, '
      'words; listening holds Wi-Fi from the reconnects and gives it back')
