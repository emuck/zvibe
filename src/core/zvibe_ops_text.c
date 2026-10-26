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
/* A dictionary word is stored as 2 packed words (6 Z-chars) in V3 and 3 packed
 * words (9 Z-chars) in V4. The maximum is a compile-time constant (array size);
 * the count in use follows the story version. */
#if ZVIBE_ENABLE_V4
#define Z_MAX_DICT_WORDS     3
#else
#define Z_MAX_DICT_WORDS     2
#endif
#define Z_DICT_WORDS         Z_VSEL(2, 3)

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
    Z_OUT("\n", 1);
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
    Z_OUT(buf + n, 6 - n);
}

/**
 * Print a single ZSCII character (opcode: print_char)
 */
void op_print_char(void) {
    char output_char = ' '; /* Default to space */
    const zWord zscii_value = G->operands[0];
#if ZVIBE_ENABLE_V4
    if (Z_IS_V4()) {
        if (zscii_value == 0) return;                      /* null prints nothing (3.8) */
        if (G->stream3_depth && zscii_value >= 155 && zscii_value <= 251) {
            z_stream3_zscii((zByte)zscii_value);           /* the table holds ZSCII, not display text */
            return;
        }
    }
#endif
    
    /* Handle ASCII printable range and newline */
    if ((zscii_value >= Z3_ZSCII_SPACE) && (zscii_value <= 126)) {
        output_char = (char)zscii_value;
    } else if (zscii_value == Z3_ZSCII_NEWLINE) {
        output_char = '\n';
    }
    
    Z_OUT(&output_char, 1);
}

/**
 * Print text, followed by newline, then return true (opcode: print_ret)
 */
void op_print_ret(void) {
    const zByte *text_ptr = zmem_get_ptr(&G->memory_state, G->logical_pc, 0);
    zSize bytes_consumed = z_print_zscii(text_ptr, 0);
    G->logical_pc += bytes_consumed;
    Z_OUT("\n", 1);
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
 * Packed addresses are multiplied by 2 in V3 and by 4 in V4.
 */
void op_print_paddr(void) {
    z_print_zscii(zmem_get_ptr(&G->memory_state, z_packed_to_byte(G->operands[0]), 0), 0);
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
    Z_OUT(&ch, 1);
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
#if ZVIBE_ENABLE_V4
                    if (Z_IS_V4() && G->stream3_depth && zscii_code >= 155 && zscii_code <= 251)
                        decoded = (char)zscii_code;        /* memory tables keep extended ZSCII */
#endif
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

/* Encode the leading input chars into Z_DICT_WORDS packed dictionary words
 * (3 Z-characters each; the last word carries the end-of-text bit).
 *
 * Each input character becomes a short run of Z-characters: a letter is one;
 * a character in alphabet 2 is a shift (5) plus one; any other character is the
 * ZSCII escape: shift, 6, then the 10-bit code as two 5-bit halves. The run is
 * appended up to the capacity (6 Z-characters in V3, 9 in V4) and the encoding
 * STOPS there: the stream is truncated, so a shift that lands in the last slot
 * stays there, and no later character may take its place (standard, section 3.7).
 * Unused slots are padded with 5. */
static void z_encode_zchars(const zByte *chars, zByte length, zWord encoded[Z_MAX_DICT_WORDS]) {
    const int limit = Z_DICT_WORDS * 3;           /* Z-chars that fit: 6 in V3, 9 in V4 */
    zByte z[Z_MAX_DICT_WORDS * 3];
    int n = 0;
    for (int k = 0; k < limit; k++) z[k] = 5;     /* pad */
    for (zByte i = 0; i < length && n < limit; i++) {
        zByte c = chars[i];
        zByte run[4];
        int run_len, j;
        if (c >= 'A' && c <= 'Z') c |= 0x20;      /* fold to lowercase */
        if (c >= 'a' && c <= 'z') {
            run[0] = (zByte)(c - 'a' + 6);
            run_len = 1;
        } else {
            const char *p = c ? strchr(Z3_A2_CHARS, c) : NULL;
            run[0] = 5;                            /* shift to A2 (a single shift in V3 and later) */
            if (p) {
                run[1] = (zByte)((p - Z3_A2_CHARS) + 7);  /* z-char = index+7 */
                run_len = 2;
            } else {
                run[1] = 6;                        /* ZSCII escape */
                run[2] = (zByte)(c >> 5);
                run[3] = (zByte)(c & 0x1F);
                run_len = 4;
            }
        }
        for (j = 0; j < run_len && n < limit; j++)
            z[n++] = run[j];
    }
    for (int w = 0; w < Z_DICT_WORDS; w++)
        encoded[w] = (zWord)((z[3 * w] << 10) | (z[3 * w + 1] << 5) | z[3 * w + 2]);
    encoded[Z_DICT_WORDS - 1] |= Z3_TEXT_TERMINATOR;
}

/* Compare dictionary entry e with the encoded word, a word at a time:
 * <0 if the entry sorts first, 0 if equal, >0 if it sorts after. */
Z_VERSION_HELPER int z_dict_compare(const zByte *e, const zWord encoded[Z_MAX_DICT_WORDS]) {
    for (int w = 0; w < Z_DICT_WORDS; w++) {
        const zWord ew = ((zWord)e[2 * w] << 8) | e[2 * w + 1];
        if (ew != encoded[w])
            return (ew < encoded[w]) ? -1 : 1;
    }
    return 0;
}

/* Binary search the sorted dictionary; returns Z-machine byte address of entry or 0. */
static zWord z_dict_lookup(const zByte *dict, zWord entries, zByte entry_len,
                            zWord base_addr, const zWord encoded[Z_MAX_DICT_WORDS]) {
    if (entries == 0) return 0;                   /* empty dictionary: nothing to find (hi would underflow) */
    zWord lo = 0, hi = entries - 1;
    while (lo <= hi) {
        zWord mid = lo + (hi - lo) / 2;
        const zByte *e = dict + (zSize)mid * entry_len;
        const int cmp = z_dict_compare(e, encoded);
        if (cmp == 0)
            return base_addr + (zWord)((zSize)mid * entry_len);
        if (cmp < 0)
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
    zWord enc[Z_MAX_DICT_WORDS];
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
