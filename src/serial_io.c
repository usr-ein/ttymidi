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

#include "serial_io.h"

#include <errno.h>
#include <unistd.h>

int write_all_ex(int fd, const void* buf, size_t len, io_write_fn wr)
{
    const unsigned char* p = (const unsigned char*)buf;
    while (len > 0)
    {
        ssize_t n = wr(fd, p, len);
        if (n < 0)
        {
            if (errno == EINTR)
                continue; /* interrupted by a signal: retry */
            return -1;
        }
        if (n == 0)
            return -1; /* no progress: bail rather than spin forever */
        p += n;
        len -= (size_t)n;
    }
    return 0;
}

int write_all(int fd, const void* buf, size_t len)
{
    return write_all_ex(fd, buf, len, write);
}
