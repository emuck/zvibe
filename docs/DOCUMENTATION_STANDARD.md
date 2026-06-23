# ZVibe Documentation Standard

This document defines Doxygen conventions for the ZVibe codebase.

## File-Level Documentation

All `.c` and `.h` files must begin with a Doxygen file header:

```c
/**
 * @file filename.c
 * @brief Brief one-line description
 * @ingroup GroupName
 *
 * Detailed module description, responsibilities, and design constraints.
 *
 * **Embedded Constraints:**
 * - RAM: Static allocation details (e.g., "16KB shared buffer")
 * - Thread safety: Reentrant/ISR-safe/not thread-safe
 * - Alignment: Pointer alignment requirements (if applicable)
 *
 * **Implementation Notes:**
 * - Algorithm choices
 * - Z-machine version-specific behavior (V3 only)
 * - Endianness (big-endian per Z-machine spec)
 */
```

## Function Documentation

### Full Documentation (Public APIs and Complex Functions)

```c
/**
 * @brief Brief description (one line, imperative mood)
 *
 * Detailed description explaining behavior, side effects, and memory ownership.
 *
 * @param[in]  param_name  Input parameter description
 * @param[out] result      Output parameter (caller-provided buffer)
 * @param[in,out] buffer   Buffer modified in place
 *
 * @return Description of return value:
 *         - ZVIBE_OK (0) on success
 *         - ZVIBE_ERROR on failure
 *         - ZVIBE_WAIT_FOR_INPUT when input needed
 *
 * @pre Preconditions (e.g., "ctx must be initialized")
 * @post Postconditions (e.g., "buffer contains valid data")
 *
 * @warning Critical safety notes (bounds, overflow, alignment)
 * @note Additional implementation details
 */
```

### Simplified Form (Simple Functions)

For trivial getters, setters, or self-explanatory functions:

```c
/** @brief Brief description */
```

## Structure and Type Documentation

```c
/**
 * @brief Brief description of structure purpose
 *
 * Ownership: Who allocates/frees this structure
 * Lifetime: When is this valid
 */
typedef struct {
    int field1;          /**< Field description */
    void *buffer;        /**< Buffer (caller owns, size >= buffer_size) */
    size_t buffer_size;  /**< Buffer size in bytes */
} TypeName;
```

Enums:

```c
/**
 * @brief Brief description of enumeration
 */
typedef enum {
    VALUE_OK = 0,        /**< Success */
    VALUE_ERROR = -1     /**< Error condition */
} EnumName;
```

## Macro Documentation

All configuration macros must be documented:

```c
/**
 * @def MACRO_NAME
 * @brief Brief description
 *
 * Default: value
 * Range: valid range or values
 * Impact: RAM/flash/performance impact
 */
#define MACRO_NAME value
```

## Module Groups

Organize code using `@ingroup`:

- **CoreInterpreter**: Main execution loop and opcode dispatch
- **MemoryAbstraction**: Split memory architecture
- **ObjectTable**: Z-machine object tree operations
- **OpcodesBase**: Control flow opcodes (call, return, branch, stack)
- **OpcodesMath**: Arithmetic and logical opcodes
- **OpcodesMem**: Memory access opcodes (load, store)
- **OpcodesText**: Text output and dictionary opcodes
- **PublicAPI**: Application-facing API (zvibe_api.h)
- **PlatformCallbacks**: Platform integration points

See `docs/groups.dox` for group definitions.

## Embedded-Specific Documentation

Always document:

1. **RAM usage**: Static buffer sizes, stack depth
2. **No malloc**: All allocations are static
3. **Thread safety**: Explicitly state if reentrant or not
4. **Alignment**: Document if pointers must be aligned
5. **Endianness**: Big-endian (Z-machine spec)
6. **Error handling**: Fatal errors vs. recoverable errors

## Conditional Compilation

Document all feature flags with impact:

```c
/**
 * @def ZVIBE_MINIMAL_FEATURES
 * @brief Disable saves and status line for minimal builds
 *
 * When defined:
 * - Disables SAVE/RESTORE/RESTART opcodes
 * - Disables status line updates
 *
 * Code size reduction: ~2KB
 * RAM reduction: ~300 bytes
 */
```

## Parameter Direction Tags

Use consistently:
- `@param[in]`: Input only, not modified
- `@param[out]`: Output only, caller provides buffer
- `@param[in,out]`: Modified in place

## Return Value Documentation

Always document all possible return values and their meanings.

For error codes, list all values:
```c
 * @return Error code:
 *         - ZMEM_SUCCESS (0) on success
 *         - ZMEM_ERROR_NULL_PTR if state is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if address invalid
```

## Memory Ownership

Document ownership explicitly:
- **Caller owns**: Caller allocates and frees
- **Callee owns**: Function allocates, caller must not free
- **Shared**: Specify lifetime and cleanup responsibility
- **Static**: Static buffer, no cleanup needed

## TODO and Verification Tags

Use sparingly:
- `@todo VERIFY: description` - For uncertain behavior that needs code review
- `@todo description` - For incomplete implementation

## Example: Complete Function

```c
/**
 * @brief Read a byte from Z-machine memory
 *
 * Routes read to RAM or flash based on address. Performs bounds checking
 * if enabled in state->bounds_checking.
 *
 * @param[in]  state  Memory state (must not be NULL)
 * @param[in]  addr   Address to read (must be < story_size)
 * @param[out] value  Pointer to store byte value (must not be NULL)
 *
 * @return Error code:
 *         - ZMEM_SUCCESS on success
 *         - ZMEM_ERROR_NULL_PTR if state or value is NULL
 *         - ZMEM_ERROR_OUT_OF_BOUNDS if addr >= story_size
 *
 * @pre state initialized via zmem_init() or zmem_init_embedded()
 * @post value contains byte at addr (if successful)
 *
 * @warning No thread safety; external synchronization required
 */
int zmem_read_byte(const zmem_state_t *state, zmem_addr_t addr, zmem_byte_t *value);
```
