# Sample Action frame transmit command

## Prepare

1. Copy ieee80211.h in linux kernel header files to this source directory.
1. Install libnl-3 and libnl-genl-3.

## Install

1. Modify IFNAME in `nltest.c` to the interface name to transmit.
1. Modify FREQ in `nltest.c` to the frequence to transmit. (if kernel version >= 5)
1. `make`

## Run

Execute `./nltest`.
An action frame will be transmitted to 01:00:5e:40:01:02.
