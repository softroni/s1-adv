# S1-ADV 0.2.213

The clock now runs on US Central time, including daylight saving, instead of Bangkok time.

The device now asks the time servers every time it connects to Wi-Fi, and every six hours
after that. Before, it skipped the check whenever its clock already looked valid, so a clock
kept across a restart was never corrected.

The timezone is a fixed setting in the firmware, `kDeviceTz`, not a device preference.
