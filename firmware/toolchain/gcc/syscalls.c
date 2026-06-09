/*
 * DAPLink-Wireless — Wireless CMSIS-DAP v2 debug probe firmware
 * Copyright (C) 2025 RinStel <me@rinx.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <errno.h>
#include <sys/types.h>

int _close(int file)
{
    (void)file;
    errno = ENOSYS;
    return -1;
}

off_t _lseek(int file, off_t offset, int direction)
{
    (void)file;
    (void)offset;
    (void)direction;
    errno = ENOSYS;
    return (off_t)-1;
}

int _read(int file, char *buffer, int length)
{
    (void)file;
    (void)buffer;
    (void)length;
    errno = ENOSYS;
    return -1;
}

int _write(int file, const char *buffer, int length)
{
    (void)file;
    (void)buffer;
    (void)length;
    errno = ENOSYS;
    return -1;
}
