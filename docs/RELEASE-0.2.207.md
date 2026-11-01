# S1-ADV 0.2.207

Fixes a restart in 0.2.206 when voice connected. That build moved voice TLS work to
core 0 at the same priority as before, which kept core 0's idle task from running. A
handshake longer than 5 seconds then tripped the task watchdog and rebooted the device.
Voice connection tasks now run at idle priority on core 0.

Includes the voice capture and text size changes from [0.2.206](RELEASE-0.2.206.md).
