// Sonar's side of the Grove port: the M5 ultrasonic unit (I2C, 0x57), read through Air Music's
// sensor object, which owns the bus. It comes after the Lab for the Lab's setup. The screen, the
// filter and the HC-SR04 are in part 062; sonar.h decides which sensor to use.
namespace {
// An I2C bus idles high, and the Lab's relay unit, wired straight to Grove, would switch on with
// it. With relays saved there, Sonar leaves the port alone and only listens on EXT. The setup is
// normally read at start-up; a read with no card would stop the Lab reading it later.
bool sonarGroveAllowed() {
  if (gSdReady && !gMeshBusOwned) labReadBook();
  return !gLabBook.direct.has(lab::Part::Relay);
}
sonar::Step sonarGroveStep(sonar::Picker& pick, uint32_t nowMs, bool echoFresh, uint32_t& mm) {
  gAirSensor.open();
  return sonar::service(pick, gAirSensor, nowMs, echoFresh, mm);
}
void sonarGroveClose() { gAirSensor.close(); }
}  // namespace
