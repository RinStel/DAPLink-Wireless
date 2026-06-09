# Third-Party Notices

These notices apply only to the identified third-party components.
DAPLink-Wireless project code is licensed separately under GNU GPL v3.0 or
later; see `LICENSE`.

## GigaDevice GD32F30x firmware library

The firmware links source files from the GigaDevice GD32F30x CMSIS,
standard peripheral and USB device libraries, version 3.0.3.

Copyright (c) 2025, GigaDevice Semiconductor Inc.

See `licenses/GigaDevice-BSD-3-Clause.txt`.

## Arm CMSIS-DAP

Arm CMSIS-DAP v2.1.2 is used as the protocol reference. `DAP.h` and the
upstream license are retained in the source tree. The asynchronous wireless
front end is an independent implementation and does not compile upstream
`DAP.c`.

Copyright Arm Limited and contributors.

In the source tree, see `Third-Party/CMSIS-DAP/LICENSE`. Release packages
include the same file as `licenses/CMSIS-DAP-Apache-2.0.txt`.

## Hardware documentation

Semtech SX1280/SX1281 and Ebyte E28 documents are design references only and
are not redistributed in the release package.
