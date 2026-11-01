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
assert network.count('nullptr, kVoiceNetworkCore)') == 2
print('voice capture core: ok')
