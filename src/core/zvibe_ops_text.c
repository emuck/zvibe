/*
 * Derived from mojozork by Ryan C. Gordon
 * https://github.com/icculus/mojozork
 * Copyright (c) 2015-2023 Ryan C. Gordon
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_ops_text.c
 * @brief Text output and input opcodes
 * @ingroup OpcodesText
 *
 * Implements text encoding, output, and input operations for Z-machine V3:
 * - Output: print, print_ret, print_addr, print_paddr, print_char, print_num
 * - Input: read (with tokenization)
 * - ZSCII encoding/decoding (3-letter alphabet, abbreviations)
 * - Window management: split_window, set_window
 * - Dictionary lookup for parser
 *
 * **Text Encoding:**
 * - ZSCII: 5-bit codes packed 3 per word (big-endian)
 * - 3 alphabets: A0 (lowercase), A1 (uppercase), A2 (symbols)
 * - Abbreviations: 3 tables × 32 entries
 * - Inline text: follows opcode until high bit set
 *
 * **Implementation Notes:**
 * - READ opcode suspends execution (returns ZVIBE_WAIT_FOR_INPUT)
 * - Text output via G->writestr callback
 * - Dictionary uses binary search for word lookup
 */

#include <stdlib.h>
#include <string.h>
/* #include <strings.h> - Removed for XC32 compatibility (unused) */
#include "zvibe.h"

/* -------------------- CONSTANTS -------------------- */

/* Text processing constants for Version 3 */
#define Z3_ZSCII_NEWLINE     13     /* Newline character in ZSCII */
#define Z3_ZSCII_SPACE       32     /* Space character in ZSCII */
#define Z3_TEXT_TERMINATOR   0x8000 /* Terminator bit for Z-strings */
#define Z3_MAX_TOKEN_LENGTH  6      /* Maximum length of token (Z-chars per word) */

/* Forward declaration of helper functions */
static char z_decode_zscii_char(zWord val);
typedef void (*z_zscii_sink_fn)(char ch, void *userdata);

/* -------------------- CHARACTER CONVERSION FUNCTIONS -------------------- */

/**
 * Decodes a single ZSCII character
 * 
 * @param val ZSCII character code
 * @return Decoded ASCII character
 */
static char z_decode_zscii_char(zWord val) {
    /* ZSCII to output character mapping */
    if ((val >= 32) && (val <= 126))
        return (char)val;
    else if (val == 13)
        return '\n';
    else if (val == 0)
        return 0;  /* No output */
    
    /* All other characters become '?' for simplicity */
    return '?';
}

#ifdef __ELF__
static char z_alphabet_table[] __attribute__((section(".data"))) =
#else
static char z_alphabet_table[] =
#endif
    "abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "\0\n"
    "0123456789.,!?_#'\"/\\-:()";

/* -------------------- TEXT OUTPUT OPERATIONS -------------------- */

/**
 * Print a newline character (opcode: new_line)
 */
void op_new_line(void) {
    if (G->writestr) {
        G->writestr("\n", 1);
    }
}

/**
 * Print the encoded text at the current program counter (opcode: print)
 * 
 * Advances the program counter past the encoded text.
 */
void op_print(void) {
    const zByte *text_ptr = zmem_get_ptr(&G->memory_state, G->logical_pc, 0);
    zSize bytes_consumed = z_print_zscii(text_ptr, 0);
    G->logical_pc += bytes_consumed;
}

/**
 * Print a signed 16-bit integer (opcode: print_num)
 * 
 * Uses an optimized integer-to-string conversion for efficiency.
 */
void op_print_num(void) {
    const zSWord value = (zSWord)G->operands[0];
    char buf[7];  /* "-32768\0" */
    int v = (int)value, n = 6;
    buf[6] = '\0';
    if (v == 0) {
        buf[--n] = '0';
    } else {
        if (v < 0) v = -v;
        while (v) { buf[--n] = '0' + v % 10; v /= 10; }
        if (value < 0) buf[--n] = '-';
    }
    if (G->writestr) G->writestr(buf + n, 6 - n);
}

/**
 * Print a single ZSCII character (opcode: print_char)
 */
void op_print_char(void) {
    char output_char = ' '; /* Default to space */
    const zWord zscii_value = G->operands[0];
    
    /* Handle ASCII printable range and newline */
    if ((zscii_value >= Z3_ZSCII_SPACE) && (zscii_value <= 126)) {
        output_char = (char)zscii_value;
    } else if (zscii_value == Z3_ZSCII_NEWLINE) {
        output_char = '\n';
    }
    
    if (G->writestr) {
        G->writestr(&output_char, 1);
    }
}

/**
 * Print text, followed by newline, then return true (opcode: print_ret)
 */
void op_print_ret(void) {
    const zByte *text_ptr = zmem_get_ptr(&G->memory_state, G->logical_pc, 0);
    zSize bytes_consumed = z_print_zscii(text_ptr, 0);
    G->logical_pc += bytes_consumed;
    if (G->writestr) {
        G->writestr("\n", 1);
    }
    z_do_return(1);
}

/**
 * Print encoded text at a given memory address (opcode: print_addr)
 */
void op_print_addr(void) {
    z_print_zscii(zmem_get_ptr(&G->memory_state, G->operands[0], 0), 0);
}

/**
 * Print encoded text at a packed address (opcode: print_paddr)
 * 
 * In Version 3, packed addresses are multiplied by 2.
 */
void op_print_paddr(void) {
    z_print_zscii(zmem_get_ptr(&G->memory_state, G->operands[0] * 2, 0), 0);
}

/**
 * Decodes a ZSCII string
 * 
 * @param _str Pointer to ZSCII string
 * @param abbr Whether this is an abbreviation
 * @param buf Buffer to store decoded string
 * @param _buflen Pointer to length of buffer, updated with characters decoded
 * @return Number of bytes read from source
 */
typedef struct {
    char *buf;
    zSize remaining;
    zSize chars;
} z_zscii_buffer_sink_t;

static void z_zscii_emit_buffer(char ch, void *userdata) {
    z_zscii_buffer_sink_t *sink = (z_zscii_buffer_sink_t *)userdata;
    sink->chars++;
    if (sink->remaining) {
        *(sink->buf++) = ch;
        sink->remaining--;
    }
}

static void z_zscii_emit_output(char ch, void *userdata) {
    (void)userdata;
    if (G->writestr) {
        G->writestr(&ch, 1);
    }
}

static zSize z_walk_zscii(const zByte *encoded_text, int is_abbreviation,
                          z_zscii_sink_fn sink, void *sink_userdata) {
    const zByte *str = encoded_text;
    zByte alphabet = 0;
    zByte use_abbr_table = 0;
    zByte zscii_collector = 0;
    zWord zscii_code = 0;
    zWord code;

    do {
        code = Z_READ16(str);

        for (zSByte i = 10; i >= 0; i -= 5) {
            int newshift = 0;
            char decoded = 0;
            const zByte ch = (zByte)((code >> i) & 0x1F);

            if (zscii_collector) {
                if (zscii_collector == 2) {
                    zscii_code |= ((zWord)ch) << 5;
                } else {
                    zscii_code |= (zWord)ch;
                }
                zscii_collector--;
                if (!zscii_collector) {
                    decoded = z_decode_zscii_char(zscii_code);
                    if (decoded && sink) {
                        sink(decoded, sink_userdata);
                    }
                    alphabet = 0;
                    use_abbr_table = 0;
                    zscii_code = 0;
                }
                continue;
            }

            if (use_abbr_table) {
                if (is_abbreviation) {
                    G->die("Abbreviation recursion not allowed");
                }
                const zSize index = (zSize)(32 * (use_abbr_table - 1)) + (zSize)ch;
                const zByte *ptr = zmem_get_ptr(&G->memory_state,
                    G->header.abbrtab_addr + (index * 2), 0);
                const zWord abbraddr = Z_READ16(ptr);
                z_walk_zscii(zmem_get_ptr(&G->memory_state, abbraddr * 2, 0), 1,
                             sink, sink_userdata);
                use_abbr_table = 0;
                alphabet = 0;
                continue;
            }

            switch (ch) {
                case 0:
                    decoded = ' ';
                    break;
                case 1:
                    use_abbr_table = 1;
                    break;
                case 2:
                case 3:
                    use_abbr_table = ch;
                    break;
                case 4:
                case 5:
                    newshift = 1;
                    alphabet = ch - 3;
                    break;
                default:
                    if ((ch == 6) && (alphabet == 2)) {
                        zscii_collector = 2;
                    } else {
                        decoded = z_alphabet_table[(alphabet * 26) + (ch - 6)];
                    }
                    break;
            }

            if (decoded && sink) {
                sink(decoded, sink_userdata);
            }

            if (alphabet && !newshift) {
                alphabet = 0;
            }
        }
    } while ((code & (1 << 15)) == 0);

    return str - encoded_text;
}

zSize z_decode_zscii(const zByte *_str, int abbr, char *buf, zSize *_buflen) {
    z_zscii_buffer_sink_t sink = {
        .buf = buf,
        .remaining = *_buflen,
        .chars = 0
    };

    const zSize bytes_read = z_walk_zscii(_str, abbr, z_zscii_emit_buffer, &sink);
    *_buflen = sink.chars;
    return bytes_read;
}

/**
 * Print ZSCII encoded text
 *
 * Streams decoded characters directly to G->writestr one at a time.
 * No fixed-size decode buffer — handles arbitrarily long strings.
 *
 * @param encoded_text Pointer to Z-encoded text
 * @param is_abbreviation Flag indicating if processing an abbreviation
 * @return Number of bytes read from encoded_text
 */
zSize z_print_zscii(const zByte *encoded_text, int is_abbreviation) {
    return z_walk_zscii(encoded_text, is_abbreviation, z_zscii_emit_output, NULL);
}

/* -------------------- TEXT INPUT AND PARSING -------------------- */

/* A2 alphabet characters (indices match z-char encoding: index+7 = z-char in A2).
 * Index 0 = \n (z-char 7), index 1 = '0' (z-char 8), ..., index 12 = ',' (z-char 19), etc.
 * Z-char 6 in A2 is the ZSCII 10-bit escape — not in this table. */
static const char Z3_A2_CHARS[] = "\n0123456789.,!?_#'\"/\\-:()";

/* Encode up to Z3_MAX_TOKEN_LENGTH input chars into two packed Z-machine dictionary words. */
static void z_encode_zchars(const zByte *chars, zByte length, zWord encoded[2]) {
    zByte z[Z3_MAX_TOKEN_LENGTH] = {5, 5, 5, 5, 5, 5};
    int n = 0;
    for (zByte i = 0; i < length; i++) {
        char c = (char)chars[i];
        if (c >= 'A' && c <= 'Z') c |= 0x20;  /* fold to lowercase */
        if (c >= 'a' && c <= 'z') {
            if (n >= Z3_MAX_TOKEN_LENGTH) break;
            z[n++] = (zByte)(c - 'a' + 6);
        } else {
            const char *p = strchr(Z3_A2_CHARS, c);
            if (p && n + 1 < Z3_MAX_TOKEN_LENGTH) {
                z[n++] = 5;  /* shift to A2 (v3 z-char 5) */
                z[n++] = (zByte)((p - Z3_A2_CHARS) + 7);  /* z-char = index+7 */
            }
        }
    }
    encoded[0] = (zWord)((z[0] << 10) | (z[1] << 5) | z[2]);
    encoded[1] = (zWord)((z[3] << 10) | (z[4] << 5) | z[5]) | Z3_TEXT_TERMINATOR;
}

/* Binary search the sorted V3 dictionary; returns Z-machine byte address of entry or 0. */
static zWord z_dict_lookup(const zByte *dict, zWord entries, zByte entry_len,
                            zWord base_addr, const zWord encoded[2]) {
    zWord lo = 0, hi = entries - 1;
    while (lo <= hi) {
        zWord mid = lo + (hi - lo) / 2;
        const zByte *e = dict + (zSize)mid * entry_len;
        zWord e0 = ((zWord)e[0] << 8) | e[1];
        zWord e1 = ((zWord)e[2] << 8) | e[3];
        if (e0 == encoded[0] && e1 == encoded[1])
            return base_addr + (zWord)((zSize)mid * entry_len);
        if (e0 < encoded[0] || (e0 == encoded[0] && e1 < encoded[1]))
            lo = mid + 1;
        else {
            if (mid == 0) break;  /* prevent zWord underflow */
            hi = mid - 1;
        }
    }
    return 0;
}

/* Emit one token (word or separator) into the parse buffer. */
static void z_emit_token(zByte **pb, const zByte *chars, zByte length, zByte position,
                          const zByte *dict, zWord entries, zByte entry_len, zWord base_addr) {
    zWord enc[2];
    z_encode_zchars(chars, length, enc);
    zWord addr = z_dict_lookup(dict, entries, entry_len, base_addr, enc);
    (*pb)[0] = (zByte)(addr >> 8);
    (*pb)[1] = (zByte)(addr & 0xFF);
    *pb += 2;
    *(*pb)++ = length;
    *(*pb)++ = position;
}

/**
 * Tokenize user input string for the Z-machine parser
 *
 * @param input_addr Address of input buffer in Z-machine memory
 * @param parse_addr Address of parse buffer in Z-machine memory
 */
static void z_tokenize_input(zWord input_addr, zWord parse_addr) {
    const zByte *input_buffer = zmem_get_ptr(&G->memory_state, input_addr, 0);
    zByte *parse_buffer       = zmem_get_ptr(&G->memory_state, parse_addr, 1);
    const zByte max_tokens    = *(parse_buffer++);

    const zByte *separators     = zmem_get_ptr(&G->memory_state, G->header.dict_addr, 0);
    const zByte separator_count = *(separators++);
    const zByte *dictionary     = separators + separator_count;
    const zByte entry_len       = *(dictionary++);
    const zWord dict_entries    = ((zWord)dictionary[0] << 8) | (zWord)dictionary[1];
    dictionary += 2;
    const zWord dict_base       = G->header.dict_addr + separator_count + 4;

    input_buffer++;   /* skip max-chars byte */
    parse_buffer++;   /* skip token-count byte (written at end) */

    const zByte *token_start = input_buffer;
    const zByte *cur         = input_buffer;
    zByte token_count        = 0;

    while (token_count < max_tokens) {
        int is_sep = (*cur == ' ' || *cur == '\0');
        if (!is_sep) {
            for (zByte i = 0; i < separator_count; i++)
                if (*cur == separators[i]) { is_sep = 1; break; }
        }

        if (is_sep) {
            /* Emit word token (if any) */
            zByte word_len = (zByte)(cur - token_start);
            if (word_len > 0 && token_count < max_tokens) {
                z_emit_token(&parse_buffer, token_start, word_len,
                             (zByte)((token_start - input_buffer) + 1),
                             dictionary, dict_entries, entry_len, dict_base);
                token_count++;
            }
            /* Emit separator token (spec: separators are tokens in their own right) */
            if (*cur != ' ' && *cur != '\0' && token_count < max_tokens) {
                z_emit_token(&parse_buffer, cur, 1,
                             (zByte)((cur - input_buffer) + 1),
                             dictionary, dict_entries, entry_len, dict_base);
                token_count++;
            }
            if (*cur == '\0') break;
            token_start = cur + 1;
        }

        if (*cur == '\0') break;
        cur++;
    }

    zmem_write_byte(&G->memory_state, parse_addr + 1, token_count);
}

/**
 * Process input data provided by the API layer
 * 
 * This function is called when input is available from the frontend.
 * 
 * @param input_text Input text to process
 */
void z_process_input(const char *input_text) {
    /* Get input buffer from Z-machine memory */
    zByte *input_buffer = zmem_get_ptr(&G->memory_state, G->input_buffer_addr, 1);
    zByte max_input_length = *input_buffer;
    input_buffer++; /* Skip length byte */
    
    /* Copy input to buffer - fix signedness comparison */
    size_t input_length = strlen(input_text);
    if (input_length > (size_t)(max_input_length - 1))
        input_length = (size_t)(max_input_length - 1);
        
    memcpy(input_buffer, input_text, input_length);
    input_buffer[input_length] = '\0';
    
    /* Tokenize input if parse buffer provided */
    if (G->parse_buffer_addr) {
        z_tokenize_input(G->input_buffer_addr, G->parse_buffer_addr);
    }

    /* Clear input request flag */
    G->input_requested = 0;
    
    /* Restore program counter to continue execution */
    G->logical_pc = G->saved_logical_pc;
}

/**
 * Read user input and tokenize it (opcode: read)
 * 
 * The Z-machine opcode that requests input. This implementation
 * sets flags for the API layer to handle the actual input.
 */
void op_read(void) {
    /* Get input buffer from Z-machine memory */
    zByte *input_buffer = zmem_get_ptr(&G->memory_state, G->operands[0], 1);
    zByte max_input_length = *input_buffer;
    
    /* Validate input buffer size */
    if (max_input_length < 3) {
        G->die("Text buffer is too small for reading (minimum 3 bytes)");
    }
    
    /* Get parse buffer from Z-machine memory (if provided) */
    zByte parse_flag = G->operand_count > 1;
    
    /* Set flags to request input from API layer */
    G->input_requested = 1;
    G->input_buffer_addr = G->operands[0];
    G->parse_buffer_addr = parse_flag ? G->operands[1] : 0;
    
    /* Save current PC to resume later */
    G->saved_logical_pc = G->logical_pc;
    
    /* Exit to API layer - execution will continue when input is provided */
}
