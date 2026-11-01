#!/usr/bin/env python3
"""Voice TLS work must not share a CPU core with the microphone drain loop."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parts = root / 'bots/CardputerADV/src/main_parts'
network = (parts / '065_main.cpp.inc').read_text()
constants = (parts / '001_main.cpp.inc').read_text()

assert 'constexpr BaseType_t kVoiceNetworkCore = 0;' in constants
assert 'xTaskCreate(voicePreconnectTask' not in network
assert network.count('xTaskCreatePinnedToCore(voicePreconnectTask') == 2
assert network.count('raw, kVoiceNetworkPriority, nullptr, kVoiceNetworkCore)') == 2
# Above idle priority, a handshake over 5 s starves core 0's idle task and the
# task watchdog reboots the device.
assert 'constexpr UBaseType_t kVoiceNetworkPriority = tskIDLE_PRIORITY;' in constants
print('voice capture core: ok')
