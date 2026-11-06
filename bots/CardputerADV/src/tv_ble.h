#pragma once
// The Cardputer as a Bluetooth keyboard, for TV Remote. It runs only while TV Remote is open with
// Bluetooth on, because the stack and the radio's controller take heap that TLS needs elsewhere.
#include <cstddef>
#include <cstdint>

namespace tv_ble {

enum class State : uint8_t { Off, Waiting, Connected, Failed };

// Starts the stack and advertises as a keyboard. False, with failure() saying why, if it can't.
bool start(const char* name, int batteryPercent);
// Stops it and gives its memory back. Paired devices stay remembered for next time.
void stop();
State state();
bool on();
// How much heap start() took, for the log: the real cost is only known on the device.
uint32_t heapCost();
const char* failure();
// Something has paired with it and is remembered, so it reconnects on its own.
bool paired();
// Forgets every paired device. Only while it's on, because the list belongs to the stack.
bool forget();

// Each presses and releases. False when nothing is connected to hear it.
bool key(uint8_t usage, uint8_t modifiers = 0);
bool media(uint16_t usage);

}  // namespace tv_ble
