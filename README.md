# Sample Action frame transmit command

## Install

1. Modify IFNAME in `nltest.c` to the interface name to transmit.
1. Modify FREQ in `nltest.c` to the frequence to transmit. (if kernel version >= 5)
1. `make`

## Run

Execute `./nltest`.
An action frame will be transmitted to 01:00:5e:40:01:02.
