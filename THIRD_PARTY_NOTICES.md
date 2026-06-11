# Third-Party Notices

These notices apply only to the identified third-party components.
DAPLink-Wireless project code is licensed separately under GNU GPL v3.0 or
later; see `LICENSE`.

## GigaDevice GD32F30x firmware library

The firmware links source files from the GigaDevice GD32F30x CMSIS,
standard peripheral and USB device libraries, version 3.0.3.

Copyright (c) 2025, GigaDevice Semiconductor Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.
3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

## Arm CMSIS-DAP

Arm CMSIS-DAP v2.1.2 is used as the protocol reference. `DAP.h` and the
upstream license are retained in the source tree. The asynchronous wireless
front end is an independent implementation and does not compile upstream
`DAP.c`.

Copyright Arm Limited and contributors.

In the source tree, see `Third-Party/CMSIS-DAP/LICENSE`. Release packages
include the same file as `CMSIS-DAP-LICENSE.txt`.

## Hardware documentation

Semtech SX1280/SX1281 and Ebyte E28 documents are design references only and
are not redistributed in the release package.
