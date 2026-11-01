# S1-ADV 0.2.223

The Wi-Fi scan tries harder before giving up. It now retries for about 10 seconds instead of
4, gives the radio a moment to stop connecting first, tries again if the results get lost, and
briefly drops the current network if a connected radio keeps refusing, then rejoins it.

When a scan does fail, the message now says why: the radio's error code and the free memory,
or the state of the connection if the results never arrived. A scan that takes too long says
it timed out.
