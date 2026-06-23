/* Copyright (c) 2025 Martin R. Raumann */
/* SPDX-License-Identifier: BSD-3-Clause */
/**
 * test_split_memory.c - Unit tests for split memory architecture
 *
 * Tests the split memory system to ensure it correctly separates
 * dynamic (RAM) and static (flash) memory regions while maintaining
 * the appearance of contiguous memory access.
 */

#include "zvibe_memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Test utilities */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST_ASSERT(condition) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf("PASS: %s\n", #condition); \
    } else { \
        printf("FAIL: %s (line %d)\n", #condition, __LINE__); \
    } \
} while(0)

#define TEST_SECTION(name) printf("\n=== %s ===\n", name)

/* Sample Z3 story data with header */
static const uint8_t test_story_data[] = {
    0x03,       /* Version 3 */
    0x00,       /* Flags1 */
    0x00, 0x01, /* Release */
    0x40, 0x00, /* Himem addr */
    0x20, 0x00, /* PC start */
    0x30, 0x00, /* Dict addr */
    0x35, 0x00, /* Object table addr */
    0x38, 0x00, /* Globals addr */
    0x00, 0x80, /* Static memory addr (128 bytes) */
    0x00, 0x00, /* Flags2 */
    /* Serial number */
    '8', '4', '0', '7', '2', '6',
    0x3A, 0x00, /* Abbrev table addr */
    0x01, 0x00, /* Story length */
    0xAB, 0xCD, /* Checksum */

    /* Pad to 128 bytes with some test data */
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
    /* ... fill rest with pattern ... */
};

/* Initialize test story data */
static uint8_t full_story_data[256];

static void init_test_data(void) {
    memcpy(full_story_data, test_story_data, sizeof(test_story_data));

    /* Fill dynamic region (0-127) with pattern */
    for (int i = sizeof(test_story_data); i < 128; i++) {
        full_story_data[i] = (uint8_t)(i & 0xFF);
    }

    /* Fill static region (128-255) with different pattern */
    for (int i = 128; i < 256; i++) {
        full_story_data[i] = (uint8_t)(0xFF - (i & 0xFF));
    }
}

/* Test basic initialization */
static void test_memory_init(void) {
    TEST_SECTION("Memory Initialization Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    /* Test successful initialization */
    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(state.ram_buffer != NULL);
    TEST_ASSERT(state.ram_size == 128);
    TEST_ASSERT(state.config.staticmem_addr == 128);

    /* Test that RAM was initialized with story data */
    TEST_ASSERT(state.ram_buffer[0] == 0x03);  /* Version */
    TEST_ASSERT(state.ram_buffer[14] == 0x00); /* Static mem addr high */
    TEST_ASSERT(state.ram_buffer[15] == 0x80); /* Static mem addr low */

    zmem_cleanup(&state);

    /* Test invalid parameters */
    result = zmem_init(NULL, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_ERROR_NULL_PTR);

    result = zmem_init(&state, NULL, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_ERROR_NULL_PTR);

    result = zmem_init(&state, &config, NULL, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_ERROR_NULL_PTR);

    /* Test invalid staticmem_addr */
    zmem_config_t bad_config = config;
    bad_config.staticmem_addr = 0;
    result = zmem_init(&state, &bad_config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_ERROR_BAD_SIZE);

    bad_config.staticmem_addr = 300;  /* > story_size */
    result = zmem_init(&state, &bad_config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_ERROR_BAD_SIZE);
}

/* Test memory validation */
static void test_memory_validation(void) {
    TEST_SECTION("Memory Validation Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);

    /* Test valid addresses */
    TEST_ASSERT(zmem_validate_addr(&state, 0, 0) == ZMEM_SUCCESS);     /* RAM read */
    TEST_ASSERT(zmem_validate_addr(&state, 127, 0) == ZMEM_SUCCESS);   /* RAM read */
    TEST_ASSERT(zmem_validate_addr(&state, 128, 0) == ZMEM_SUCCESS);   /* Flash read */
    TEST_ASSERT(zmem_validate_addr(&state, 255, 0) == ZMEM_SUCCESS);   /* Flash read */

    TEST_ASSERT(zmem_validate_addr(&state, 0, 1) == ZMEM_SUCCESS);     /* RAM write */
    TEST_ASSERT(zmem_validate_addr(&state, 127, 1) == ZMEM_SUCCESS);   /* RAM write */

    /* Test invalid addresses */
    TEST_ASSERT(zmem_validate_addr(&state, 256, 0) == ZMEM_ERROR_OUT_OF_BOUNDS);
    TEST_ASSERT(zmem_validate_addr(&state, 128, 1) == ZMEM_ERROR_WRITE_PROTECTED); /* Flash write */
    TEST_ASSERT(zmem_validate_addr(&state, 255, 1) == ZMEM_ERROR_WRITE_PROTECTED); /* Flash write */

    /* Test RAM/Flash detection */
    TEST_ASSERT(zmem_is_ram_addr(&state, 0) == 1);
    TEST_ASSERT(zmem_is_ram_addr(&state, 127) == 1);
    TEST_ASSERT(zmem_is_ram_addr(&state, 128) == 0);
    TEST_ASSERT(zmem_is_ram_addr(&state, 255) == 0);

    zmem_cleanup(&state);
}

/* Test byte operations */
static void test_byte_operations(void) {
    TEST_SECTION("Byte Operation Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);

    zmem_byte_t value;

    /* Test reading from RAM */
    result = zmem_read_byte(&state, 0, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == 0x03);  /* Version byte */

    result = zmem_read_byte(&state, 50, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == 50);  /* Pattern data */

    /* Test reading from Flash */
    result = zmem_read_byte(&state, 128, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == (0xFF - 128));  /* Flash pattern */

    result = zmem_read_byte(&state, 200, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == (0xFF - 200));  /* Flash pattern */

    /* Test writing to RAM */
    result = zmem_write_byte(&state, 50, 0xAB);
    TEST_ASSERT(result == ZMEM_SUCCESS);

    result = zmem_read_byte(&state, 50, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == 0xAB);

    /* Test writing to Flash (should fail) */
    result = zmem_write_byte(&state, 128, 0xCD);
    TEST_ASSERT(result == ZMEM_ERROR_WRITE_PROTECTED);

    /* Test out of bounds */
    result = zmem_read_byte(&state, 256, &value);
    TEST_ASSERT(result == ZMEM_ERROR_OUT_OF_BOUNDS);

    result = zmem_write_byte(&state, 256, 0xEF);
    TEST_ASSERT(result == ZMEM_ERROR_OUT_OF_BOUNDS);

    zmem_cleanup(&state);
}

/* Test word operations */
static void test_word_operations(void) {
    TEST_SECTION("Word Operation Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);

    zmem_word_t value;

    /* Test reading word from RAM */
    result = zmem_read_word(&state, 0, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == 0x0300);  /* Version + Flags1 */

    /* Test reading word from Flash */
    result = zmem_read_word(&state, 128, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == (((0xFF - 128) << 8) | (0xFF - 129)));

    /* Test writing word to RAM */
    result = zmem_write_word(&state, 50, 0x1234);
    TEST_ASSERT(result == ZMEM_SUCCESS);

    result = zmem_read_word(&state, 50, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == 0x1234);

    /* Verify individual bytes */
    zmem_byte_t byte_val;
    result = zmem_read_byte(&state, 50, &byte_val);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(byte_val == 0x12);

    result = zmem_read_byte(&state, 51, &byte_val);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(byte_val == 0x34);

    /* Test writing word to Flash (should fail) */
    result = zmem_write_word(&state, 128, 0x5678);
    TEST_ASSERT(result == ZMEM_ERROR_WRITE_PROTECTED);

    /* Test cross-boundary word (should work for read) */
    result = zmem_read_word(&state, 127, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    /* Should read byte 127 from RAM and byte 128 from Flash */

    /* Test cross-boundary word write (should fail) */
    result = zmem_write_word(&state, 127, 0x9ABC);
    TEST_ASSERT(result == ZMEM_ERROR_WRITE_PROTECTED);

    zmem_cleanup(&state);
}

/* Test pointer access */
static void test_pointer_access(void) {
    TEST_SECTION("Pointer Access Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);

    /* Test RAM pointer access */
    zmem_byte_t *ptr = zmem_get_ptr(&state, 50, 0);  /* Read */
    TEST_ASSERT(ptr != NULL);
    TEST_ASSERT(*ptr == 50);  /* Pattern data */

    ptr = zmem_get_ptr(&state, 50, 1);  /* Write */
    TEST_ASSERT(ptr != NULL);
    *ptr = 0xAB;

    /* Verify write worked */
    zmem_byte_t value;
    result = zmem_read_byte(&state, 50, &value);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(value == 0xAB);

    /* Test Flash pointer access */
    ptr = zmem_get_ptr(&state, 200, 0);  /* Read */
    TEST_ASSERT(ptr != NULL);
    TEST_ASSERT(*ptr == (0xFF - 200));

    ptr = zmem_get_ptr(&state, 200, 1);  /* Write (should fail) */
    TEST_ASSERT(ptr == NULL);

    /* Test out of bounds */
    ptr = zmem_get_ptr(&state, 256, 0);
    TEST_ASSERT(ptr == NULL);

    zmem_cleanup(&state);
}

/* Test contiguous memory appearance */
static void test_contiguous_memory(void) {
    TEST_SECTION("Contiguous Memory Appearance Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);

    /* Test that memory appears contiguous by reading across boundary */
    zmem_byte_t ram_byte, flash_byte;

    result = zmem_read_byte(&state, 127, &ram_byte);    /* Last RAM byte */
    TEST_ASSERT(result == ZMEM_SUCCESS);

    result = zmem_read_byte(&state, 128, &flash_byte);  /* First Flash byte */
    TEST_ASSERT(result == ZMEM_SUCCESS);

    /* Verify different storage but seamless access */
    TEST_ASSERT(ram_byte == 127);        /* Pattern in RAM */
    TEST_ASSERT(flash_byte == (0xFF - 128)); /* Pattern in Flash */

    /* Test sequential access across boundary */
    for (int i = 120; i < 135; i++) {
        zmem_byte_t value;
        result = zmem_read_byte(&state, i, &value);
        TEST_ASSERT(result == ZMEM_SUCCESS);

        if (i < 128) {
            TEST_ASSERT(value == i);  /* RAM pattern */
        } else {
            TEST_ASSERT(value == (0xFF - i));  /* Flash pattern */
        }
    }

    zmem_cleanup(&state);
}

/* Test memory statistics */
static void test_memory_stats(void) {
    TEST_SECTION("Memory Statistics Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);

    size_t ram_used;
    zmem_get_stats(&state, &ram_used);
    TEST_ASSERT(ram_used == 128);

    zmem_cleanup(&state);
}

/* Test embedded init/cleanup ownership */
static void test_embedded_cleanup(void) {
    TEST_SECTION("Embedded Cleanup Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };
    uint8_t ram_buffer[128];
    memset(ram_buffer, 0xCC, sizeof(ram_buffer));

    int result = zmem_init_embedded(&state, &config, full_story_data, sizeof(full_story_data), ram_buffer);
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(state.ram_buffer == ram_buffer);
    TEST_ASSERT(state.owns_ram_buffer == 0);
    TEST_ASSERT(ram_buffer[0] == 0x03);

    zmem_cleanup(&state);

    TEST_ASSERT(state.ram_buffer == NULL);
    TEST_ASSERT(state.ram_size == 0);
    TEST_ASSERT(ram_buffer[0] == 0x03);
}

static void test_embedded_init_validation(void) {
    TEST_SECTION("Embedded Init Validation Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };
    uint8_t ram_buffer[128];

    int result = zmem_init_embedded(NULL, &config, full_story_data, sizeof(full_story_data), ram_buffer);
    TEST_ASSERT(result == ZMEM_ERROR_NULL_PTR);

    result = zmem_init_embedded(&state, NULL, full_story_data, sizeof(full_story_data), ram_buffer);
    TEST_ASSERT(result == ZMEM_ERROR_NULL_PTR);

    result = zmem_init_embedded(&state, &config, NULL, sizeof(full_story_data), ram_buffer);
    TEST_ASSERT(result == ZMEM_ERROR_NULL_PTR);

    result = zmem_init_embedded(&state, &config, full_story_data, sizeof(full_story_data), NULL);
    TEST_ASSERT(result == ZMEM_ERROR_NULL_PTR);
}

static void test_flash_access(void) {
    TEST_SECTION("Flash Access Tests");

    zmem_state_t state;
    zmem_config_t config = {
        .staticmem_addr = 128,
        .story_size = 256
    };

    int result = zmem_init(&state, &config, full_story_data, sizeof(full_story_data));
    TEST_ASSERT(result == ZMEM_SUCCESS);
    TEST_ASSERT(state.flash_data == full_story_data);
    TEST_ASSERT(state.flash_size == sizeof(full_story_data));

    zmem_byte_t *ptr = zmem_get_ptr(&state, 200, 0);
    TEST_ASSERT(ptr != NULL);
    TEST_ASSERT(*ptr == (0xFF - 200));

    zmem_cleanup(&state);
}

/* Main test runner */
int main(void) {
    printf("Split Memory Architecture Unit Tests\n");
    printf("====================================\n");

    /* Initialize test data */
    init_test_data();

    /* Run all tests */
    test_memory_init();
    test_memory_validation();
    test_byte_operations();
    test_word_operations();
    test_pointer_access();
    test_contiguous_memory();
    test_memory_stats();
    test_embedded_cleanup();
    test_embedded_init_validation();
    test_flash_access();

    /* Print results */
    printf("\n====================================\n");
    printf("Tests run: %d\n", tests_run);
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_run - tests_passed);

    if (tests_passed == tests_run) {
        printf("All tests PASSED!\n");
        return 0;
    } else {
        printf("Some tests FAILED!\n");
        return 1;
    }
}
