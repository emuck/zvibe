/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * zvibe_v4term.c - Play a Z-machine V4 story in an ANSI terminal
 *
 * The interpreter, the ANSI/VT100 screen layer (src/screen) and a small
 * terminal frontend: it asks the terminal how big it is, puts it in raw mode,
 * edits input lines itself (so the screen layer always knows where the cursor
 * is), turns cursor keys into ZSCII for read_char, and saves games to a file.
 *
 * This is the reference for how a frontend uses the V4 API: zvibe_set_screen(),
 * ZVIBE_WAIT_FOR_CHAR / zvibe_input_char(), and routing the story's text through
 * the screen layer.
 *
 * Terminals narrower than 56 columns are not supported; wider ones use 80.
 * zvibe_console uses this automatically for a version 4 story when both stdin
 * and stdout are terminals (and no -s script is given); everything else, and all
 * version 3 stories, take the console's plain line-oriented path. A command line
 * must fit on the terminal line (there is no horizontal scrolling); further keys
 * ring the bell. POSIX terminals only.
 */

#include <errno.h>
#include <signal.h>
#include <sys/select.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "zvibe_api.h"
#include "zvibe_screen.h"
#include "zvibe_v4term.h"

#if ZVIBE_ENABLE_V4 && !defined(_WIN32)

static zscreen scr;
static FILE *transcript;                  /* -t: the story's text, as printed */
static FILE *commands;                    /* -c: every command line typed */
static struct termios saved_tio;
static struct termios raw_tio;
static volatile sig_atomic_t raw_on;
static volatile sig_atomic_t need_resume;   /* set by SIGTSTP/continue: re-establish the screen's modes */

/* ---- terminal ---- */

static void out_byte(uint8_t b) {
    putchar(b);
}

static void raw_mode(int on);

/** Put the terminal back; used on every way out, including signals. */
static void restore_terminal(void) {
    raw_mode(0);
}

static const char reset_seq[] = "\033[0m\033[?7h\033[r\r\n";

/** Fatal signals (and abort): put the terminal back, then die of the same signal. */
static void on_signal(int sig) {
    if (raw_on) {
        ssize_t n = write(STDOUT_FILENO, reset_seq, sizeof reset_seq - 1);
        (void)n;
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

/** Ctrl-Z: give the shell a sane terminal, stop, and take the terminal back when continued. */
static void on_tstp(int sig) {
    ssize_t n = write(STDOUT_FILENO, reset_seq, sizeof reset_seq - 1);
    (void)n;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    signal(sig, SIG_DFL);
    {   /* SIGTSTP is blocked while its own handler runs; unblock it so the stop happens here,
         * after the terminal was given back and before it is taken again */
        sigset_t unblock;
        sigemptyset(&unblock);
        sigaddset(&unblock, sig);
        sigprocmask(SIG_UNBLOCK, &unblock, NULL);
    }
    raise(sig);                                  /* stops here until SIGCONT */
    signal(sig, on_tstp);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw_tio);
    need_resume = 1;                             /* the main loop repairs the screen's modes */
}

static void raw_mode(int on) {
    struct termios t;
    if (on == raw_on) return;
    if (on) {
        tcgetattr(STDIN_FILENO, &saved_tio);
        t = saved_tio;
        t.c_lflag &= ~(ICANON | ECHO);          /* keys arrive one at a time and are echoed by us */
        t.c_iflag &= ~(ICRNL | IXON);
        t.c_cc[VMIN] = 1;
        t.c_cc[VTIME] = 0;
        raw_tio = t;
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    } else {
        tcsetattr(STDIN_FILENO, TCSANOW, &saved_tio);
    }
    raw_on = on;
}

static void terminal_size(uint8_t *rows, uint8_t *cols) {
    struct winsize ws;
    *rows = 24; *cols = 80;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row >= 10 && ws.ws_col >= 56) {
        *rows = (ws.ws_row > 250) ? 250 : (uint8_t)ws.ws_row;
        *cols = (ws.ws_col > ZSCREEN_MAX_COLS) ? ZSCREEN_MAX_COLS : (uint8_t)ws.ws_col;
    }
}

/* ---- keys ---- */

static int pushed_back = -1;                  /* a byte read past the end of an escape, to be read again */

/** True if a key is already waiting, or arrives within 50 ms (an escape sequence's tail). */
static int key_pending(void) {
    fd_set fds;
    struct timeval tv;
    if (pushed_back >= 0) return 1;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    tv.tv_sec = 0; tv.tv_usec = 50000;
    return select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0;
}

static int read_key_byte(void) {
    unsigned char c;
    ssize_t n;
    if (pushed_back >= 0) { int b = pushed_back; pushed_back = -1; return b; }
    do { n = read(STDIN_FILENO, &c, 1); } while (n < 0 && errno == EINTR);
    if (need_resume) {                           /* continued after Ctrl-Z */
        need_resume = 0;
        zscreen_resume(&scr);
        fflush(stdout);
    }
    return n == 1 ? c : -1;
}

/**
 * One key as the ZSCII code a story sees: 13 for Return, 8 for Delete, 27 for a
 * lone Escape, 129-132 for the cursor keys; escape sequences that are not a
 * cursor key are swallowed. Returns -1 at end of input.
 */
static int read_zscii_key(void) {
    for (;;) {
        int c = read_key_byte();
        if (c < 0) return -1;
        if (c == '\r' || c == '\n') return 13;
        if (c == 8 || c == 127) return 8;                 /* ZSCII delete (3.8) */
        if (c == 27) {
            int c2 = key_pending() ? read_key_byte() : -1;
            if (c2 != '[' && c2 != 'O') {                      /* a lone Escape; the next key is its own */
                pushed_back = c2;
                return 27;
            }
            c2 = key_pending() ? read_key_byte() : -1;
            if (c2 == 'A') return 129;
            if (c2 == 'B') return 130;
            if (c2 == 'D') return 131;
            if (c2 == 'C') return 132;
            /* skip the rest of an unknown sequence; if it stops short, the next key is not part of it */
            while (c2 >= 0 && !(c2 >= 0x40 && c2 <= 0x7E)) c2 = key_pending() ? read_key_byte() : -1;
            continue;
        }
        if (c >= 32 && c < 127) return c;
    }
}

/** Read an edited line, echoing through the screen layer. Returns 0 on end of input. */
static int read_line(char *buf, size_t size) {
    size_t len = 0;
    zscreen_flush(&scr);
    fflush(stdout);
    for (;;) {
        int k = read_zscii_key();
        if (k < 0) return 0;
        if (k == 13) { zscreen_echo(&scr, '\n'); break; }
        if (k == 8) {
            if (len && zscreen_echo(&scr, 8)) len--;
        } else if (k >= 32 && k < 127 && len + 1 < size) {
            if (zscreen_echo(&scr, (char)k)) buf[len++] = (char)k;
            else putchar('\a');                           /* the line is full */
        }
        fflush(stdout);
    }
    buf[len] = 0;
    if (transcript) fprintf(transcript, "%s\n", buf);
    if (commands) { fprintf(commands, "%s\n", buf); fflush(commands); }
    return 1;
}

/* ---- story text ---- */

static void story_text(const char *text, size_t len) {
    size_t i;
    for (i = 0; i < len; i++) zscreen_putc(&scr, text[i]);
    if (transcript) fwrite(text, 1, len, transcript);
}

/* ---- save files ---- */

static char save_path[1024];

static void do_save(zvibeContext *ctx) {
    size_t size = zvibe_get_save_size(ctx);
    void *data = malloc(size);
    int ok = 0;
    if (data && zvibe_get_save_data(ctx, data, size) == size) {
        FILE *f = fopen(save_path, "wb");
        if (f) { ok = fwrite(data, 1, size, f) == size; fclose(f); }
    }
    free(data);
    zvibe_save_completed(ctx, ok);
}

static void do_restore(zvibeContext *ctx) {
    int ok = 0;
    FILE *f = fopen(save_path, "rb");
    if (f) {
        long cap = -1;
        if (fseek(f, 0, SEEK_END) == 0) cap = ftell(f);
        rewind(f);
        if (cap > 0 && cap <= (1L << 20)) {          /* a V4 save can be over 64 KB */
            void *data = malloc((size_t)cap);
            size_t n = data ? fread(data, 1, (size_t)cap, f) : 0;
            if (n == (size_t)cap) ok = zvibe_restore_data(ctx, data, n) == ZVIBE_OK;
            free(data);
        }
        fclose(f);
    }
    zvibe_restore_completed(ctx, ok);
}

/* ---- main ---- */

int zvibe_v4term_wants(const char *story_file) {
    FILE *f = fopen(story_file, "rb");
    int version = f ? fgetc(f) : -1;
    if (f) fclose(f);
    return version == 4 && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
}

int zvibe_v4term_play(const char *story_file, int seed, const char *save_file, FILE *transcript_out, FILE *commands_out) {
    zvibeContext *ctx;
    uint8_t rows, cols;
    char line[256];
    int finished = 0;

    snprintf(save_path, sizeof save_path, "%s", save_file);
    transcript = transcript_out;
    commands = commands_out;

    ctx = zvibe_create(story_text);
    if (!ctx) return 1;
    terminal_size(&rows, &cols);
    zscreen_init(&scr, out_byte, rows, cols);
    zvibe_set_screen(ctx, &zscreen_ops, &scr);
    zvibe_set_random_seed(ctx, seed);
    if (zvibe_load_story(ctx, story_file) != ZVIBE_OK) {
        zscreen_shutdown(&scr);
        fprintf(stderr, "cannot load %s\n", story_file);
        return 1;
    }

    atexit(restore_terminal);
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGHUP, on_signal);
    signal(SIGQUIT, on_signal);
    signal(SIGABRT, on_signal);                  /* abort() skips atexit handlers */
    signal(SIGTSTP, on_tstp);
    raw_mode(1);
    while (!finished) {
        zvibeResult r = zvibe_run(ctx);
        zscreen_flush(&scr);
        fflush(stdout);
        switch (r) {
        case ZVIBE_WAIT_FOR_CHAR: {
            int k = read_zscii_key();
            if (k < 0) { finished = 1; break; }
            zvibe_input_char(ctx, (uint16_t)k);
            break;
        }
        case ZVIBE_WAIT_FOR_INPUT:
            if (!read_line(line, sizeof line)) { finished = 1; break; }
            zvibe_input(ctx, line);
            break;
        case ZVIBE_SAVE_REQUESTED:    do_save(ctx); break;
        case ZVIBE_RESTORE_REQUESTED: do_restore(ctx); break;
        case ZVIBE_RESTART_REQUESTED: zvibe_restart_completed(ctx); break;
        case ZVIBE_GAME_FINISHED:     finished = 1; break;
        default:                      finished = 1; break;
        }
    }
    zscreen_flush(&scr);
    zscreen_shutdown(&scr);
    fflush(stdout);
    raw_mode(0);
    zvibe_destroy(ctx);
    return 0;
}

#endif /* ZVIBE_ENABLE_V4 && !_WIN32 */
