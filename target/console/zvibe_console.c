/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * zvibe_console.c - Console frontend with status line
 * 
 * Features:
 * - Interactive mode: Status line with simple line input
 * - Script mode: Clean text output for automated testing  
 * - Logging: Transcript (full session) and commands-only files
 * 
 * Architecture:
 * - Mode-aware design: Interactive features disabled in script mode for clean output
 * - Simple input handling: Standard line-based input for reliability
 * - Centralized file I/O: Single functions for logging and cleanup
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include "zvibe_api.h"

/* Platform-specific includes */
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <signal.h>
    #define STDOUT_FILENO 1
    #ifndef PATH_MAX
        #define PATH_MAX MAX_PATH
    #endif
#else
    #include <unistd.h>
    #include <sys/ioctl.h>
    #include <signal.h>
#endif

/* ==================== CONFIGURATION ==================== */

#define DEFAULT_TERM_WIDTH  80
#define DEFAULT_TERM_HEIGHT 24
#define STATUS_LINE_SIZE    160

/* ==================== GLOBAL STATE ==================== */

/* Operating mode */
static int script_mode = 0;  /* 1 if running in script mode (clean output) */

/* Terminal state (interactive mode only) */
static int term_width = DEFAULT_TERM_WIDTH;
static int term_height = DEFAULT_TERM_HEIGHT;
static char status_line[STATUS_LINE_SIZE] = "";

/* File I/O */
static FILE *script_file = NULL;        /* Input: script commands */
static FILE *transcript_file = NULL;    /* Output: commands + game output */
static FILE *commands_file = NULL;      /* Output: commands only */

/* Save file path */
static char save_filename[PATH_MAX] = "";

/* Set up save filename based on game filename */
static void setup_save_filename(const char *game_file) {
    if (!game_file) {
        save_filename[0] = '\0';
        return;
    }
    
    /* Create save filename by replacing .z3 extension with .sav */
    strncpy(save_filename, game_file, PATH_MAX - 1);
    save_filename[PATH_MAX - 1] = '\0';
    
    /* Find the last dot to replace extension */
    char *dot = strrchr(save_filename, '.');
    if (dot && (strcmp(dot, ".z3") == 0 || strcmp(dot, ".Z3") == 0)) {
        strcpy(dot, ".sav");
    } else {
        /* No .z3 extension, just append .sav */
        size_t len = strlen(save_filename);
        if (len < PATH_MAX - 5) {
            strcat(save_filename, ".sav");
        } else {
            save_filename[0] = '\0';  /* Path too long */
        }
    }
}

/* ==================== UTILITY FUNCTIONS ==================== */

/* Log a command to transcript and commands files */
static void log_command(const char *command) {
    if (!command || *command == '\0') return;
    
    if (transcript_file) {
        fprintf(transcript_file, "%s\n", command);
        fflush(transcript_file);
    }
    if (commands_file) {
        fprintf(commands_file, "%s\n", command);
        fflush(commands_file);
    }
}

/* Close all open files */
static void close_files(void) {
    if (script_file) {
        fclose(script_file);
        script_file = NULL;
    }
    if (transcript_file) {
        fclose(transcript_file);
        transcript_file = NULL;
    }
    if (commands_file) {
        fclose(commands_file);
        commands_file = NULL;
    }
}

/* ==================== TERMINAL CONTROL ==================== */

/* Get current terminal dimensions */
#ifdef _WIN32
static void get_terminal_size(void) {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        term_width = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        term_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
}
#else
static void get_terminal_size(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        term_width = ws.ws_col;
        term_height = ws.ws_row;
    }
}
#endif


/* Terminal control sequences */
static void clear_screen(void) {
    printf("\033[2J\033[H");
    fflush(stdout);
}

static void goto_xy(int x, int y) {
    printf("\033[%d;%dH", y, x);
}

static void save_cursor(void) {
    printf("\033[s");
}

static void restore_cursor(void) {
    printf("\033[u");
}

static void set_reverse_video(void) {
    printf("\033[7m");
}

static void set_normal_video(void) {
    printf("\033[0m");
}

static void set_scroll_region(int top, int bottom) {
    printf("\033[%d;%dr", top, bottom);
}

/* Status line display */
static void draw_status_line(void) {
    save_cursor();
    goto_xy(1, 1);
    set_reverse_video();
    
    /* Clear the line */
    for (int i = 0; i < term_width; i++) {
        putchar(' ');
    }
    
    /* Position and print status */
    goto_xy(1, 1);
    printf("%.*s", term_width, status_line);
    
    set_normal_video();
    restore_cursor();
    fflush(stdout);
}

/* Position cursor below status line */
static void position_for_game(void) {
    goto_xy(1, 3); /* Leave line 2 blank for separation */
}

/* ==================== ZVIBE CALLBACKS ==================== */
static void output_callback(const char *text, size_t length) {
    /* In script mode, avoid all terminal control sequences */
    if (!script_mode) {
        /* Ensure we're positioned correctly for game output */
        static int first_output = 1;
        if (first_output) {
            position_for_game();
            first_output = 0;
        }
    }

    fwrite(text, 1, length, stdout);
    /* No fflush here — stdout is line-buffered on ttys (auto-flushes on \n),
     * and we do an explicit flush+status-redraw in ZVIBE_WAIT_FOR_INPUT before
     * each input prompt. Flushing per character was O(chars) status redraws. */

    /* Write to transcript file if open */
    if (transcript_file) {
        fwrite(text, 1, length, transcript_file);
        fflush(transcript_file);
    }
}

/* Read command from script file */
static size_t read_script_input(char *buffer, size_t max_length) {
    if (!buffer || max_length == 0 || max_length > INT_MAX || 
        fgets(buffer, (int)max_length, script_file) == NULL) {
        /* End of script file */
        fclose(script_file);
        script_file = NULL;
        if (!script_mode) {
            printf("\n[End of script - switching to interactive mode]\n");
        }
        return 0; /* Signal to fallback to other input mode */
    }
    
    /* Clean up the input line */
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }
    
    /* Echo the scripted command */
    printf("%s\n", buffer);
    fflush(stdout);
    
    /* Log command to output files */
    log_command(buffer);
    
    return len;
}

/* Simple line input for script mode or non-interactive terminals */
static size_t read_simple_input(char *buffer, size_t max_length) {
    if (!buffer || max_length == 0) return 0;
    
    /* Only draw status line in interactive mode */
    if (!script_mode) {
        draw_status_line();
        fflush(stdout);
    }
    
    if (max_length > INT_MAX || fgets(buffer, (int)max_length, stdin) == NULL) {
        return 0;
    }
    
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
        len--;
    }
    
    /* Log command to output files */
    log_command(buffer);
    
    return len;
}

static size_t input_callback(char *buffer, size_t max_length) {
    /* Try script input first */
    if (script_file) {
        size_t len = read_script_input(buffer, max_length);
        if (len > 0 || script_file) return len; /* Continue if we got input or script is still open */
    }
    
    /* Use simple line input for all interactive input */
    return read_simple_input(buffer, max_length);
}

#if ZVIBE_ENABLE_STATUS_LINE
static void status_callback(const zvibeStatus *status) {
    /* Don't draw status line in script mode */
    if (script_mode) return;
    
    /* Format status line with location on left, score/time on right */
    int left_len = snprintf(status_line, sizeof(status_line), " %s", status->location);
    int right_len = strlen(status->status);
    
    
    /* Calculate spacing */
    int spaces = term_width - left_len - right_len - 2;
    if (spaces < 1) spaces = 1;
    
    /* Add spaces and right-aligned status */
    for (int i = 0; i < spaces && left_len < (int)sizeof(status_line) - right_len - 1; i++) {
        status_line[left_len++] = ' ';
    }
    
    /* Add right-aligned status */
    snprintf(status_line + left_len, sizeof(status_line) - left_len, "%s ", status->status);
    
    draw_status_line();
}
#endif

#if ZVIBE_ENABLE_SAVE_RESTORE
static int save_callback(const void *data, size_t size) {
    if (save_filename[0] == '\0') {
        printf("\n[Error: No save filename set]\n");
        return 0;
    }
    
    FILE *file = fopen(save_filename, "wb");
    if (!file) {
        printf("\n[Error: Cannot create save file %s]\n", save_filename);
        return 0;
    }
    
    size_t written = fwrite(data, 1, size, file);
    fclose(file);
    
    if (written != size) {
        printf("\n[Error: Failed to write save data]\n");
        return 0;
    }
    
    printf("\n[Game saved to %s]\n", save_filename);
    return 1;
}

static size_t restore_callback(void *buffer, size_t max_size) {
    if (save_filename[0] == '\0') {
        printf("\n[Error: No save filename set]\n");
        return 0;
    }
    
    FILE *file = fopen(save_filename, "rb");
    if (!file) {
        printf("\n[No save file found: %s]\n", save_filename);
        return 0;
    }
    
    /* Get file size */
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);
    
    if (file_size <= 0 || file_size > (long)max_size) {
        printf("\n[Error: Invalid save file size]\n");
        fclose(file);
        return 0;
    }
    
    size_t read_bytes = fread(buffer, 1, file_size, file);
    fclose(file);
    
    if (read_bytes != (size_t)file_size) {
        printf("\n[Error: Failed to read save data]\n");
        return 0;
    }
    
    printf("\n[Game restored from %s]\n", save_filename);
    return read_bytes;
}
#endif

/* Signal handler for cleanup */
static void cleanup_handler(int sig) {
    (void)sig;
    close_files();
    
    if (!script_mode) {
        /* Ensure we have current terminal size */
        if (term_height == DEFAULT_TERM_HEIGHT) get_terminal_size();
        
        /* Reset terminal state but don't clear screen */
        set_scroll_region(1, term_height);
        set_normal_video();
        printf("\n");
        fflush(stdout);
    }
    
    exit(0);
}

static void print_usage(const char *program_name) {
    fprintf(stderr, "Usage: %s [options] <story.z3>\n", program_name);
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -s, --script <file>      Read commands from script file\n");
    fprintf(stderr, "  -t, --transcript <file>  Write both commands and game output\n");
    fprintf(stderr, "  -c, --commands <file>    Write only user commands\n");
    fprintf(stderr, "  -r, --seed <N>           Set RNG seed (default: time-based)\n");
    fprintf(stderr, "  -h, --help               Show this help message\n");
}

/* ==================== MAIN PROGRAM ==================== */

int main(int argc, char *argv[]) {
    char *story_file = NULL;
    char *script_filename = NULL;
    char *transcript_filename = NULL;
    char *commands_filename = NULL;
    int rng_seed_set = 0;
    int rng_seed = 0;

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else if (strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--script") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: Script file name required\n");
                return 1;
            }
            script_filename = argv[i];
        }
        else if (strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--transcript") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: Transcript file name required\n");
                return 1;
            }
            transcript_filename = argv[i];
        }
        else if (strcmp(argv[i], "-c") == 0 || strcmp(argv[i], "--commands") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: Commands file name required\n");
                return 1;
            }
            commands_filename = argv[i];
        }
        else if (strcmp(argv[i], "-r") == 0 || strcmp(argv[i], "--seed") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "Error: Seed value required\n");
                return 1;
            }
            rng_seed = atoi(argv[i]);
            rng_seed_set = 1;
        }
        else if (argv[i][0] == '-') {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
        else {
            if (story_file) {
                fprintf(stderr, "Error: Multiple story files specified\n");
                return 1;
            }
            story_file = argv[i];
        }
    }
    
    if (!story_file) {
        fprintf(stderr, "Error: No story file specified\n");
        print_usage(argv[0]);
        return 1;
    }
    
    /* Open script file if specified */
    if (script_filename) {
        script_file = fopen(script_filename, "r");
        if (!script_file) {
            fprintf(stderr, "Error: Cannot open script file '%s'\n", script_filename);
            return 1;
        }
        script_mode = 1;  /* We're running in script mode */
    }
    
    /* Open transcript file if specified */
    if (transcript_filename) {
        transcript_file = fopen(transcript_filename, "w");
        if (!transcript_file) {
            fprintf(stderr, "Error: Cannot create transcript file '%s'\n", transcript_filename);
            return 1;
        }
    }
    
    /* Open commands file if specified */
    if (commands_filename) {
        commands_file = fopen(commands_filename, "w");
        if (!commands_file) {
            fprintf(stderr, "Error: Cannot create commands file '%s'\n", commands_filename);
            return 1;
        }
    }
    
    /* Setup signal handling for clean exit */
#ifdef _WIN32
    /* Windows uses SetConsoleCtrlHandler for Ctrl+C handling */
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);
    /* SIGPIPE doesn't exist on Windows */
#else
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);
    signal(SIGPIPE, cleanup_handler);
#endif
    
    /* Setup terminal (only in interactive mode) */
    if (!script_mode) {
        get_terminal_size();
        clear_screen();
        
        /* Set scroll region to protect status line */
        set_scroll_region(3, term_height); /* Lines 3 to bottom can scroll */
    }
    
    /* Create context */
    zvibeContext *ctx = zvibe_create(output_callback);
    if (!ctx) {
        fprintf(stderr, "Failed to create context\n");
        return 1;
    }

    /* Seed RNG: fixed seed if -r given, otherwise time-based */
    zvibe_set_random_seed(ctx, rng_seed_set ? rng_seed : (int)time(NULL));
    
#if ZVIBE_ENABLE_STATUS_LINE
    /* Set status callback (only in interactive mode) */
    if (!script_mode) {
        zvibe_set_status_callback(ctx, status_callback);
    }
#endif

    /* Load story */
    if (zvibe_load_story(ctx, story_file) != ZVIBE_OK) {
        fprintf(stderr, "Failed to load story\n");
        zvibe_destroy(ctx);
        return 1;
    }
    
    /* Set up save filename for persistent saves */
    setup_save_filename(story_file);
    
    /* Initialize status line (only in interactive mode) */
    if (!script_mode) {
        strcpy(status_line, " zVibe Z-Machine Interpreter");
        draw_status_line();
        position_for_game();
        
#if ZVIBE_ENABLE_STATUS_LINE
        /* Get initial status and force update */
        zvibeStatus initial_status;
        if (zvibe_get_status(ctx, &initial_status) == ZVIBE_OK) {
            status_callback(&initial_status);
        }
#endif
    }
    
    /* Game loop */
    char input_buffer[256];
    while (1) {
        zvibeResult result = zvibe_run(ctx);
        
        switch (result) {
            case ZVIBE_WAIT_FOR_INPUT: {
                /* Flush buffered output and update status line once before prompt */
                fflush(stdout);
                if (!script_mode)
                    draw_status_line();
                size_t input_len = input_callback(input_buffer, sizeof(input_buffer));
                if (input_len > 0 || strlen(input_buffer) == 0) {
                    zvibe_input(ctx, input_buffer);
                } else {
                    goto cleanup;
                }
                break;
            }
                
#if ZVIBE_ENABLE_SAVE_RESTORE
            case ZVIBE_SAVE_REQUESTED: {
                /* Get save data from Z-machine and write to file */
                static char save_buffer[65536]; /* Static buffer for file writing */
                size_t save_size = zvibe_get_save_size(ctx);
                
                if (save_size > 0 && save_size <= sizeof(save_buffer)) {
                    /* Get actual save data from Z-machine */
                    size_t actual_size = zvibe_get_save_data(ctx, save_buffer, sizeof(save_buffer));
                    if (actual_size > 0) {
                        /* Call save callback to write to file */
                        int success = save_callback(save_buffer, actual_size);
                        zvibe_save_completed(ctx, success);
                    } else {
                        zvibe_save_completed(ctx, 0);
                    }
                } else {
                    zvibe_save_completed(ctx, 0);
                }
                break;
            }
            
            case ZVIBE_RESTORE_REQUESTED: {
                /* Read save data from file and restore */
                static char restore_buffer[65536]; /* Static buffer for file reading */
                size_t restore_size = restore_callback(restore_buffer, sizeof(restore_buffer));
                
                if (restore_size > 0) {
                    /* Restore the data into Z-machine */
                    zvibeResult result = zvibe_restore_data(ctx, restore_buffer, restore_size);
                    zvibe_restore_completed(ctx, result == ZVIBE_OK);
                } else {
                    zvibe_restore_completed(ctx, 0);
                }
                break;
            }
            
            case ZVIBE_RESTART_REQUESTED:
                printf("\n[Restarting game]\n");
                zvibe_restart_completed(ctx);
                break;
#endif
                
            case ZVIBE_GAME_FINISHED:
                printf("\n[Game finished]\n");
                goto cleanup;
                
            default:
                fprintf(stderr, "Error: %d\n", result);
                goto cleanup;
        }
    }
    
cleanup:
    zvibe_destroy(ctx);
    
    close_files();
    
    /* Reset terminal state but don't clear screen */
    if (!script_mode) {
        /* Reset scroll region and video attributes only */
        set_scroll_region(1, term_height);
        set_normal_video();
        printf("\n");  /* Just add a newline for clean prompt */
        fflush(stdout);
    }
    
    return 0;
}
