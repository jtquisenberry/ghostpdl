/* Copyright (C) 2001-2023 Artifex Software, Inc.
   All Rights Reserved.

   This software is provided AS-IS with no warranty, either express or
   implied.

   This software is distributed under license and may not be copied,
   modified or distributed except as expressly authorized under the terms
   of the license contained in the file LICENSE in this distribution.

   Refer to licensing information at http://www.artifex.com or contact
   Artifex Software, Inc.,  39 Mesa Street, Suite 108A, San Francisco,
   CA 94129, USA, for further information.
*/


/* File stream implementation using direct OS calls */
/******
 ****** NOTE: THIS FILE MAY NOT COMPILE ON NON-UNIX PLATFORMS, AND MAY
 ****** REQUIRE EDITING ON SOME UNIX PLATFORMS.
 ******/
#include "stdio_.h"		/* includes std.h */
#include "errno_.h"
#include "memory_.h"
#include "unistd_.h"            /* for read, write, fsync, lseek */

#include "gdebug.h"
#include "gpcheck.h"
#include "stream.h"
#include "strimpl.h"

/*
 * This is an alternate implementation of file streams.  It still uses
 * FILE * in the interface, but it uses direct OS calls for I/O.
 * It also includes workarounds for the nasty habit of System V Unix
 * of breaking out of read and write operations with EINTR, EAGAIN,
 * and/or EWOULDBLOCK "errors".
 *
 * The interface should be identical to that of sfxstdio.c.  However, in
 * order to allow both implementations to exist in the same executable, we
 * optionally use different names for sread_file, swrite_file, and
 * sappend_file, and omit sread_subfile (the public procedures).
 * See sfxboth.c.
 */
#ifdef KEEP_FILENO_API
/* Provide prototypes to avoid compiler warnings. */
void
    sread_fileno(stream *, gp_file *, byte *, uint),
    swrite_fileno(stream *, gp_file *, byte *, uint),
    sappend_fileno(stream *, gp_file *, byte *, uint);
#else
#  define sread_fileno sread_file
#  define swrite_fileno swrite_file
#  define sappend_fileno sappend_file
#endif

/* Forward references for file stream procedures */
static int
    s_fileno_available(stream *, gs_offset_t *),
    s_fileno_read_seek(stream *, gs_offset_t),
    s_fileno_read_close(stream *),
    s_fileno_read_process(stream_state *, stream_cursor_read *,
                          stream_cursor_write *, bool);
static int
    s_fileno_write_seek(stream *, long),
    s_fileno_write_flush(stream *),
    s_fileno_write_close(stream *),
    s_fileno_write_process(stream_state *, stream_cursor_read *,
                           stream_cursor_write *, bool);
static int
    s_fileno_switch(stream *, bool);

/* Get the file descriptor number of a stream. */
static inline int
sfileno(const stream *s)
{
    return fileno(s->file);
}

/* Get the current position of a file descriptor. */
static inline gs_offset_t
ltell(int fd)
{
    return lseek(fd, 0L, SEEK_CUR);
}

/* Define the System V interrupts that require retrying a call. */
static bool
errno_is_retry(int errn)
{
    switch (errn) {
#ifdef EINTR
    case EINTR: return true;
#endif
#if defined(EAGAIN) && (!defined(EINTR) || EAGAIN != EINTR)
    case EAGAIN: return true;
#endif
#if defined(EWOULDBLOCK) && (!defined(EINTR) || EWOULDBLOCK != EINTR) && (!defined(EAGAIN) || EWOULDBLOCK != EAGAIN)
    case EWOULDBLOCK: return true;
#endif
    default: return false;
    }
}

/* ------ File reading ------ */

/* Initialize a stream for reading an OS file. */
void
sread_fileno(register stream * s, gp_file * file, byte * buf, uint len)
{
    static const stream_procs p = {
        s_fileno_available, s_fileno_read_seek, s_std_read_reset,
        s_std_read_flush, s_fileno_read_close, s_fileno_read_process,
        s_fileno_switch
    };
    /*
     * There is no really portable way to test seekability,
     * but this should work on most systems.
     */
    int fd = fileno(file);
    long curpos = ltell(fd);
    bool seekable = (curpos != -1L && lseek(fd, curpos, SEEK_SET) != -1L);

    s_std_init(s, buf, len, &p,
               (seekable ? s_mode_read + s_mode_seek : s_mode_read));
    if_debug2m('s', s->memory, "[s]read file="PRI_INTPTR", fd=%d\n",
               (intptr_t) file, fileno(file));
    s->file = file;
    s->file_modes = s->modes;
    s->file_offset = 0;
    s->file_limit = S_FILE_LIMIT_MAX;
}

/* Confine reading to a subfile.  This is primarily for reusable streams. */
/*
 * We omit this procedure if we are also include sfxstdio.c, which has an
 * identical definition.
 */
#ifndef KEEP_FILENO_API
int
sread_subfile(stream *s, gs_offset_t start, gs_offset_t length)
{
    if (s->file == 0 || s->modes != s_mode_read + s_mode_seek ||
        s->file_offset != 0 || s->file_limit != S_FILE_LIMIT_MAX ||
        ((s->position < start || s->position > start + length) &&
         sseek(s, start) < 0)
        )
        return ERRC;
    s->position -= start;
    s->file_offset = start;
    s->file_limit = length;
    return 0;
}
#endif

/* Procedures for reading from a file */
static int
s_fileno_available(register stream * s, gs_offset_t *pl)
{
    gs_offset_t max_avail = s->file_limit - stell(s);
    gs_offset_t buf_avail = sbufavailable(s);
    int fd = sfileno(s);

    *pl = min(max_avail, buf_avail);
    if (sseekable(s)) {
        gs_offset_t pos, end;

        pos = ltell(fd);
        if (pos < 0)
            return ERRC;
        end = lseek(fd, 0L, SEEK_END);
        if (lseek(fd, pos, SEEK_SET) < 0 || end < 0)
            return ERRC;
        buf_avail += end - pos;
        *pl = min(max_avail, buf_avail);
        if (*pl == 0)
            *pl = -1;		/* EOF */
    } else {
        if (*pl == 0)
            *pl = -1;		/* EOF */
    }
    return 0;
}
static int
s_fileno_read_seek(register stream * s, gs_offset_t pos)
{
    gs_offset_t end = s->cursor.r.limit - s->cbuf + 1;
    gs_offset_t offset = pos - s->position;

    if (offset >= 0 && offset <= end) {  /* Staying within the same buffer */
        s->cursor.r.ptr = s->cbuf + offset - 1;
        return 0;
    }
    if (pos < 0 || pos > s->file_limit ||
        lseek(sfileno(s), s->file_offset + pos, SEEK_SET) < 0
        )
        return ERRC;
    s->cursor.r.ptr = s->cursor.r.limit = s->cbuf - 1;
    s->end_status = 0;
    s->position = pos;
    return 0;
}
static int
s_fileno_read_close(stream * s)
{
    gp_file *file = s->file;

    if (file != 0) {
        s->file = 0;
        return (fclose(file) ? ERRC : 0);
    }
    return 0;
}

/* Process a buffer for a file reading stream. */
/* This is the first stream in the pipeline, so pr is irrelevant. */
static int
s_fileno_read_process(stream_state * st, stream_cursor_read * ignore_pr,
                      stream_cursor_write * pw, bool last)
{
    stream *s = (stream *)st;	/* no separate state */
    int fd = sfileno(s);
    uint max_count;
    int nread, status;

again:
    max_count = pw->limit - pw->ptr;
    status = 1;
    if (s->file_limit < S_FILE_LIMIT_MAX) {
        gs_offset_t limit_count = s->file_offset + s->file_limit - ltell(fd);

        if (max_count > limit_count)
            max_count = limit_count, status = EOFC;
    }
    /*
     * In the Mac MetroWerks compiler, the prototype for read incorrectly
     * declares the second argument of read as char * rather than void *.
     * Work around this here.
     */
    nread = read(fd, (void *)(pw->ptr + 1), max_count);
    if (nread > 0)
        pw->ptr += nread;
    else if (nread == 0)
        status = EOFC;
    else if (errno_is_retry(errno))	/* Handle System V interrupts */
        goto again;
    else
        status = ERRC;
    process_interrupts(s->memory);
    return status;
}

/* ------ File writing ------ */

/* Initialize a stream for writing an OS file. */
void
swrite_fileno(register stream * s, gp_file * file, byte * buf, uint len)
{
    static const stream_procs p = {
        s_std_noavailable, s_fileno_write_seek, s_std_write_reset,
        s_fileno_write_flush, s_fileno_write_close, s_fileno_write_process,
        s_fileno_switch
    };

    s_std_init(s, buf, len, &p,
               (file == stdout ? s_mode_write : s_mode_write + s_mode_seek));
    if_debug2m('s', s->memory, "[s]write file="PRI_INTPTR", fd=%d\n",
               (intptr_t) file, fileno(file));
    s->file = file;
    s->file_modes = s->modes;
    s->file_offset = 0;		/* in case we switch to reading later */
    s->file_limit = S_FILE_LIMIT_MAX;
}
/* Initialize for appending to an OS file. */
void
sappend_fileno(register stream * s, gp_file * file, byte * buf, uint len)
{
    swrite_fileno(s, file, buf, len);
    s->modes = s_mode_write + s_mode_append;	/* no seek */
    s->file_modes = s->modes;
    s->position = lseek(fileno(file), 0L, SEEK_END);
}
/* Procedures for writing on a file */
static int
s_fileno_write_seek(stream * s, gs_offset_t pos)
{
    /* We must flush the buffer to reposition. */
    int code = sflush(s);

    if (code < 0)
        return code;
    if (lseek(sfileno(s), pos, SEEK_SET) < 0)
        return ERRC;
    s->position = pos;
    return 0;
}
static int
s_fileno_write_flush(register stream * s)
{
    int result = s_process_write_buf(s, false);

    discard(fsync(sfileno(s)));
    return result;
}
static int
s_fileno_write_close(register stream * s)
{
    s_process_write_buf(s, true);
    return s_fileno_read_close(s);
}

/* Process a buffer for a file writing stream. */
/* This is the last stream in the pipeline, so pw is irrelevant. */
static int
s_fileno_write_process(stream_state * st, stream_cursor_read * pr,
                       stream_cursor_write * ignore_pw, bool last)
{
    int nwrite, status;
    uint count;

again:
    count = pr->limit - pr->ptr;
    /* Some versions of the DEC C library on AXP architectures */
    /* give an error on write if the count is zero! */
    if (count == 0) {
        process_interrupts((stream*)st->memory);
        return 0;
    }
    /* See above regarding the Mac MetroWorks compiler. */
    nwrite = write(sfileno((stream *)st), (const void *)(pr->ptr + 1), count);
    if (nwrite >= 0) {
        pr->ptr += nwrite;
        status = 0;
    } else if (errno_is_retry(errno))	/* Handle System V interrupts */
        goto again;
    else
        status = ERRC;
    process_interrupts((stream *)st->memory);
    return status;
}

/* ------ File switching ------ */

/* Switch a file stream to reading or writing. */
static int
s_fileno_switch(stream * s, bool writing)
{
    uint modes = s->file_modes;
    int fd = sfileno(s);
    long pos;

    if (writing) {
        if (!(s->file_modes & s_mode_write))
            return ERRC;
        pos = stell(s);
        if_debug2m('s', s->memory, "[s]switch "PRI_INTPTR" to write at %ld\n",
                   (intptr_t)s, pos);
        lseek(fd, pos, SEEK_SET);	/* pacify OS */
        if (modes & s_mode_append) {
            sappend_file(s, s->file, s->cbuf, s->cbsize);  /* sets position */
        } else {
            swrite_file(s, s->file, s->cbuf, s->cbsize);
            s->position = pos;
        }
        s->modes = modes;
    } else {
        if (!(s->file_modes & s_mode_read))
            return ERRC;
        pos = stell(s);
        if_debug2m('s', s->memory, "[s]switch "PRI_INTPTR" to read at %ld\n",
                   (intptr_t) s, pos);
        if (sflush(s) < 0)
            return ERRC;
        lseek(fd, 0L, SEEK_CUR);	/* pacify OS */
        sread_file(s, s->file, s->cbuf, s->cbsize);
        s->modes |= modes & s_mode_append;	/* don't lose append info */
        s->position = pos;
    }
    s->file_modes = modes;
    return 0;
}
