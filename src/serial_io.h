/*
    This file is part of ttymidi.

    ttymidi is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    ttymidi is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ttymidi.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * Small serial-write helper, kept free of ALSA and of ttymidi's globals so it
 * can be unit tested on any host.
 */

#ifndef TTYMIDI_SERIAL_IO_H
#define TTYMIDI_SERIAL_IO_H

#include <stddef.h>    /* size_t */
#include <sys/types.h> /* ssize_t */

/*
 * Write the whole buffer to fd, looping over short writes and retrying on EINTR,
 * so a multi-byte message (notably a SysEx) is never truncated on the wire.
 * Returns 0 once every byte is written, -1 on a hard error (including a write
 * that reports 0 bytes, which would otherwise spin forever).
 */
int write_all(int fd, const void* buf, size_t len);

/*
 * Same as write_all but with the write primitive injected. Production uses the
 * write(2) syscall (see write_all); tests pass a fake to exercise short writes,
 * EINTR retries, and error paths deterministically without real I/O.
 */
typedef ssize_t (*io_write_fn)(int fd, const void* buf, size_t len);
int write_all_ex(int fd, const void* buf, size_t len, io_write_fn wr);

#endif /* TTYMIDI_SERIAL_IO_H */
