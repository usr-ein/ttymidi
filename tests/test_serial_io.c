/* Unit tests for src/serial_io.c (no ALSA). The interesting logic is write_all's
   short-write / EINTR loop; a real pipe covers the happy path and injected fake
   writers cover the partial-write, retry, and error branches deterministically. */

#include "greatest.h"
#include "serial_io.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

/* --------------------------------------------------------------------- */
/* Instrumented fake io_write_fn: records bytes, call count, and fd.      */

static unsigned char g_sink[4096];
static size_t g_sink_len;
static int g_calls;
static int g_chunk; /* max bytes "written" per call (0 = all offered) */
static int g_fd_seen;
static int g_eintr_left; /* number of leading EINTR failures to simulate */

static void fake_reset(int chunk)
{
    memset(g_sink, 0, sizeof(g_sink));
    g_sink_len   = 0;
    g_calls      = 0;
    g_chunk      = chunk;
    g_fd_seen    = -1;
    g_eintr_left = 0;
}

/* Accepts at most g_chunk bytes per call, appending them to g_sink. */
static ssize_t fake_write(int fd, const void* buf, size_t len)
{
    g_calls++;
    g_fd_seen   = fd;
    size_t take = len;
    if (g_chunk > 0 && take > (size_t)g_chunk)
        take = (size_t)g_chunk;
    if (take > sizeof(g_sink) - g_sink_len)
        take = sizeof(g_sink) - g_sink_len;
    memcpy(g_sink + g_sink_len, buf, take);
    g_sink_len += take;
    return (ssize_t)take;
}

/* Fails with EINTR g_eintr_left times, then delegates to fake_write. */
static ssize_t fake_write_eintr(int fd, const void* buf, size_t len)
{
    if (g_eintr_left > 0)
    {
        g_eintr_left--;
        g_calls++;
        errno = EINTR;
        return -1;
    }
    return fake_write(fd, buf, len);
}

static ssize_t fake_write_eio(int fd, const void* buf, size_t len)
{
    (void)fd;
    (void)buf;
    (void)len;
    g_calls++;
    errno = EIO;
    return -1;
}

static ssize_t fake_write_zero(int fd, const void* buf, size_t len)
{
    (void)fd;
    (void)buf;
    (void)len;
    g_calls++;
    return 0;
}

/* --------------------------------------------------------------------- */

TEST wa_delivers_all_via_real_pipe(void)
{
    /* Exercises the real write(2)-backed path end to end. */
    int fds[2];
    ASSERT_EQ(0, pipe(fds));
    const unsigned char msg[] = {0xF0, 0x7D, 0x01, 0x02, 0x03, 0x04, 0xF7};
    ASSERT_EQ(0, write_all(fds[1], msg, sizeof(msg)));

    unsigned char got[sizeof(msg)];
    ssize_t r = read(fds[0], got, sizeof(got));
    ASSERT_EQ((ssize_t)sizeof(msg), r);
    ASSERT_MEM_EQ(msg, got, sizeof(msg));

    close(fds[0]);
    close(fds[1]);
    PASS();
}

TEST wa_reassembles_across_partial_writes(void)
{
    /* One byte accepted per call: the loop must keep going until all are sent,
       in order, with no duplication or loss. */
    fake_reset(1);
    const unsigned char msg[] = {10, 20, 30, 40, 50, 60, 70, 80};
    ASSERT_EQ(0, write_all_ex(7, msg, sizeof(msg), fake_write));
    ASSERT_EQ((int)sizeof(msg), g_calls); /* exactly one call per byte */
    ASSERT_EQ((int)sizeof(msg), (int)g_sink_len);
    ASSERT_MEM_EQ(msg, g_sink, sizeof(msg));
    ASSERT_EQ(7, g_fd_seen); /* fd threaded through unchanged */
    PASS();
}

TEST wa_retries_on_eintr(void)
{
    /* EINTR must be retried, not treated as an error or a written byte. */
    fake_reset(0);
    g_eintr_left              = 2;
    const unsigned char msg[] = {1, 2, 3, 4, 5};
    ASSERT_EQ(0, write_all_ex(3, msg, sizeof(msg), fake_write_eintr));
    ASSERT_EQ((int)sizeof(msg), (int)g_sink_len);
    ASSERT_MEM_EQ(msg, g_sink, sizeof(msg));
    ASSERT_EQ(3, g_calls); /* 2 EINTR + 1 successful (chunk 0 = all at once) */
    PASS();
}

TEST wa_reports_hard_error(void)
{
    /* A non-EINTR failure propagates as -1 and delivers nothing. */
    fake_reset(0);
    const unsigned char msg[] = {1, 2, 3};
    ASSERT_EQ(-1, write_all_ex(3, msg, sizeof(msg), fake_write_eio));
    ASSERT_EQ(0, (int)g_sink_len);
    PASS();
}

TEST wa_stops_on_zero_write(void)
{
    /* A write returning 0 makes no progress; write_all must bail, not spin. */
    fake_reset(0);
    const unsigned char msg[] = {1, 2, 3};
    ASSERT_EQ(-1, write_all_ex(3, msg, sizeof(msg), fake_write_zero));
    ASSERT_EQ(1, g_calls);
    PASS();
}

TEST wa_empty_buffer_is_noop(void)
{
    /* Zero length: succeed without ever calling write. */
    fake_reset(0);
    ASSERT_EQ(0, write_all_ex(3, NULL, 0, fake_write));
    ASSERT_EQ(0, g_calls);
    PASS();
}

SUITE(serial_io_suite)
{
    RUN_TEST(wa_delivers_all_via_real_pipe);
    RUN_TEST(wa_reassembles_across_partial_writes);
    RUN_TEST(wa_retries_on_eintr);
    RUN_TEST(wa_reports_hard_error);
    RUN_TEST(wa_stops_on_zero_write);
    RUN_TEST(wa_empty_buffer_is_noop);
}

GREATEST_MAIN_DEFS();

int main(int argc, char** argv)
{
    GREATEST_MAIN_BEGIN();
    RUN_SUITE(serial_io_suite);
    GREATEST_MAIN_END();
}
