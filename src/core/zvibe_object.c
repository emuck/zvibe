/*
 * Derived from mojozork by Ryan C. Gordon
 * https://github.com/icculus/mojozork
 * Copyright (c) 2015-2023 Ryan C. Gordon
 * Copyright (c) 2025 Martin R. Raumann
 * SPDX-License-Identifier: BSD-3-Clause
 */
/**
 * @file zvibe_object.c
 * @brief Z-machine object table operations
 * @ingroup ObjectTable
 *
 * Implements object tree navigation and property access for Z-machine V3 and,
 * when built with ZVIBE_ENABLE_V4, V4.
 *
 * **Object table layout (the object count and sizes differ by version):**
 *
 * | | V3 | V4 |
 * |---|---|---|
 * | property defaults | 31 words | 63 words |
 * | object entry | 9 bytes | 14 bytes |
 * | attributes | 32 (4 bytes) | 48 (6 bytes) |
 * | parent/sibling/child | 1 byte each | 2 bytes each |
 * | objects | 255 | 65535 |
 * | property header | 1 byte: id 5 bits, size 3 bits | 1 or 2 bytes: id 6 bits, size 1-64 |
 *
 * **Key Operations:**
 * - Tree navigation: parent, sibling, child
 * - Attribute access: test, set, clear
 * - Property access: get, set, enumerate
 * - Object insertion/removal from tree
 *
 * The version differences are confined to the layout macros and the helpers
 * below (z_obj_field, z_field_get/set, z_prop_header, z_prop_len_at); everything
 * else is written once. In a V3-only build the macros are constants and the
 * helpers collapse to the original byte accesses.
 */

#include "zvibe.h"

/* Layout constants; Z_VSEL() picks the V3 or V4 value (a constant in a V3-only build) */
#define OBJ_SIZE          Z_VSEL(9, 14)       /* Size of an object entry */
#define OBJ_PROP_DEFAULTS Z_VSEL(31, 63)      /* Number of property defaults */
#define OBJ_ATTR_COUNT    Z_VSEL(32, 48)      /* Number of attributes */
#define OBJ_MAX_OBJECTS   Z_VSEL(255, 65535)  /* Maximum object count */
#define OBJ_REL_WIDTH     Z_VSEL(1, 2)        /* Bytes in a parent/sibling/child field */
#define OBJ_REL_BASE      Z_VSEL(4, 6)        /* Offset of the parent field (after the attributes) */
#define OBJ_PROP_OFFSET   Z_VSEL(7, 12)       /* Offset of the properties address field */

/** Which relation field of an object (index into the three consecutive fields) */
enum { REL_PARENT = 0, REL_SIBLING = 1, REL_CHILD = 2 };

/* Address of an object's relation field (parent, sibling or child). A macro, not
 * a function, so that with a constant `which` the offset folds to a constant
 * even without optimization, exactly like the original fixed offsets. */
#define z_obj_field(objptr, which) ((objptr) + OBJ_REL_BASE + (which) * OBJ_REL_WIDTH)

/* Read a relation field: one byte in V3, a big-endian word in V4. A macro for
 * the same reason as z_obj_field: nothing is called or spilled in a V3 build. */
#if ZVIBE_ENABLE_V4
#define z_field_get(field) (Z_IS_V4() ? (zWord)z_read16_be_impl(field) : (zWord)*(field))
#else
#define z_field_get(field) ((zWord)*(field))
#endif

/** Write a relation field */
Z_VERSION_HELPER void z_field_set(zByte *field, zWord value) {
#if ZVIBE_ENABLE_V4
    if (Z_IS_V4()) {
        Z_WRITE16(field, value);
        return;
    }
#endif
    *field = (zByte)value;
}

/**
 * Decode the header of the property that starts at p.
 *
 * @param p Pointer to the property's first header byte
 * @param[out] num Property number
 * @param[out] size Size of the property data in bytes
 * @return Length of the header in bytes (the data follows it)
 *
 * V3: one byte, size-1 in the top 3 bits, number in the low 5.
 * V4: number in the low 6 bits. If bit 7 is set a second byte follows whose
 * low 6 bits give the size (0 means 64); otherwise bit 6 gives the size, 1 or 2.
 */
Z_VERSION_HELPER zByte z_prop_header(const zByte *p, zWord *num, zByte *size) {
#if ZVIBE_ENABLE_V4
    if (Z_IS_V4()) {
        const zByte info = p[0];
        *num = info & 0x3F;
        if (info & 0x80) {
            const zByte n = p[1] & 0x3F;
            *size = n ? n : 64;
            return 2;
        }
        *size = (info & 0x40) ? 2 : 1;
        return 1;
    }
#endif
    *num = p[0] & 0x1F;                    /* 5 bits for prop id */
    *size = ((p[0] >> 5) & 0x7) + 1;       /* 3 bits for size */
    return 1;
}

/**
 * Size of the property whose data starts at data, found by looking back at the
 * byte before it (used by get_prop_len, which is given a data address).
 */
Z_VERSION_HELPER zByte z_prop_len_at(const zByte *data) {
    const zByte info = data[-1];
#if ZVIBE_ENABLE_V4
    if (Z_IS_V4()) {
        if (info & 0x80) {                 /* second header byte: size in the low 6 bits */
            const zByte n = info & 0x3F;
            return n ? n : 64;
        }
        return (info & 0x40) ? 2 : 1;
    }
#endif
    return ((info >> 5) & 0x7) + 1;        /* 3 bits for size */
}

/**
 * Gets a pointer to an object in the object table
 * 
 * @param objid Object ID to lookup
 * @return Pointer to object data in story memory, or NULL for object 0
 */
zByte *z_get_obj_ptr(zWord objid) {
    if (objid == 0)
        return NULL;  /* Handle special case of object 0 */
        
    if (objid > OBJ_MAX_OBJECTS)
        G->die("Invalid object id referenced");
        
    zByte *ptr = zmem_get_ptr(&G->memory_state, G->header.objtab_addr, 0);
    ptr += OBJ_PROP_DEFAULTS * 2;           /* Skip properties defaults table */
    ptr += OBJ_SIZE * (objid-1);            /* Find object in table */
    return ptr;
}

/**
 * Gets a pointer to an object's parent
 * 
 * @param objptr Pointer to object data
 * @return Pointer to parent object or NULL
 */
static zByte *z_get_obj_parent_ptr(const zByte *objptr) {
    if (!objptr) return NULL;  /* Handle null object pointer */
    const zWord parent = z_field_get(z_obj_field((zByte *)objptr, REL_PARENT));
    return parent ? z_get_obj_ptr(parent) : NULL;
}

static zByte *z_get_obj_property_table(zWord objid) {
    zByte *ptr = z_get_obj_ptr(objid);
    if (!ptr) return NULL;

    ptr += OBJ_PROP_OFFSET;
    ptr = zmem_get_ptr(&G->memory_state, Z_READ16(ptr), 0);
    if (!ptr) return NULL;
    return ptr + ((*ptr * 2) + 1);
}

/**
 * Removes an object from its parent
 * 
 * @param _objid Object ID to remove
 */
static void z_unparent_obj(zWord _objid) {
    zByte *objptr = z_get_obj_ptr(_objid);
    if (!objptr) return;  /* Handle null object */
    
    zByte *parentptr = z_get_obj_parent_ptr(objptr);
    if (parentptr) {
        zByte *field = z_obj_field(parentptr, REL_CHILD);  /* Parent's child field */
        
        /* If not direct child, scan through siblings until we find it */
        while (z_field_get(field) != _objid) {
            field = z_obj_field(z_get_obj_ptr(z_field_get(field)), REL_SIBLING);
        }
        
        /* Replace with object's sibling */
        z_field_set(field, z_field_get(z_obj_field(objptr, REL_SIBLING)));
    }
}

/**
 * Gets a pointer to an object property
 * 
 * @param objid Object ID
 * @param propid Property ID (or 0xFFFFFFFF for first property)
 * @param _size Pointer to store property size
 * @return Pointer to property data or NULL if not found
 */
zByte *z_get_obj_property(zWord objid, zDWord propid, zByte *_size) {
    zByte *ptr = z_get_obj_property_table(objid);
    if (!ptr) return NULL;
    
    while (1) {
        if (*ptr == 0) break;              /* End of properties */
        
        zWord num;
        zByte size;
        ptr += z_prop_header(ptr, &num, &size);
        
        if ((num == propid) || (propid == 0xFFFFFFFF)) {
            if (_size)
                *_size = size;
            return ptr;
        } else if (num < propid) {         /* Past it */
            break;
        }
        
        ptr += size;                       /* Try next property */
    }
    
    return NULL;
}

/**
 * Gets a pointer to an object's name
 * 
 * @param objid Object ID
 * @return Pointer to object name (ZSCII encoded)
 */
const zByte *z_get_obj_name(zWord objid) {
    const zByte *ptr = z_get_obj_ptr(objid);
    if (!ptr) return NULL;  /* Handle null object */
    
    ptr += OBJ_PROP_OFFSET;                /* Skip to properties address field */
    const zWord addr = Z_READ16(ptr);
    const zByte *name = zmem_get_ptr(&G->memory_state, addr, 0);
    if (!name || *name == 0) return NULL;  /* No short name: the "text" would be the next property */
    return name + 1;                       /* Skip the z-char count */
}

/**
 * Gets a default property value
 * 
 * @param propid Property ID
 * @return Default value for property
 */
zWord z_get_default_property(zWord propid) {
    if (propid > OBJ_PROP_DEFAULTS) {
        return 0;
    }
    
    const zByte *values = zmem_get_ptr(&G->memory_state, G->header.objtab_addr, 0);
    values += (propid-1) * 2;
    return Z_READ16(values);
}

/* Object opcode implementations */

/**
 * Tests if an object has an attribute (opcode: test_attr)
 */
void op_test_attr(void) {
    const zWord objid = G->operands[0];
    const zWord attrid = G->operands[1];
    
    if (attrid >= OBJ_ATTR_COUNT)
        G->die("Invalid attribute ID: %u", (unsigned int)attrid);
        
    zByte *ptr = z_get_obj_ptr(objid);
    if (!ptr) {
        z_do_branch(0);  /* Object 0 has no attributes */
        return;
    }
    
    ptr += (attrid / 8);
    z_do_branch((*ptr & (0x80 >> (attrid & 7))) ? 1 : 0);
}

/**
 * Sets an attribute on an object (opcode: set_attr)
 */
void op_set_attr(void) {
    const zWord objid = G->operands[0];
    const zWord attrid = G->operands[1];
    
    if (attrid >= OBJ_ATTR_COUNT)
        G->die("Invalid attribute ID: %u", (unsigned int)attrid);
        
    zByte *ptr = z_get_obj_ptr(objid);
    if (!ptr) return;  /* Ignore operation on object 0 */
    
    ptr += (attrid / 8);
    *ptr |= 0x80 >> (attrid & 7);
}

/**
 * Clears an attribute on an object (opcode: clear_attr)
 */
void op_clear_attr(void) {
    const zWord objid = G->operands[0];
    const zWord attrid = G->operands[1];
    
    if (objid == 0) return;  /* Handle Zork 1 edge case */
    
    if (attrid >= OBJ_ATTR_COUNT)
        G->die("Invalid attribute ID: %u", (unsigned int)attrid);
        
    zByte *ptr = z_get_obj_ptr(objid);
    if (!ptr) return;  /* Should never happen since we checked objid == 0 */
    
    ptr += (attrid / 8);
    *ptr &= ~(0x80 >> (attrid & 7));
}

/**
 * Inserts an object as child of another object (opcode: insert_obj)
 */
void op_insert_obj(void) {
    const zWord objid = G->operands[0];
    const zWord dstid = G->operands[1];
    
    zByte *objptr = z_get_obj_ptr(objid);
    zByte *dstptr = z_get_obj_ptr(dstid);
    
    z_unparent_obj(objid);  /* Take object out of tree first */
    
    /* Now reinsert in the right place */
    z_field_set(z_obj_field(objptr, REL_PARENT), dstid);         /* Parent: new destination */
    z_field_set(z_obj_field(objptr, REL_SIBLING),
                z_field_get(z_obj_field(dstptr, REL_CHILD)));    /* Sibling: dest's old child */
    z_field_set(z_obj_field(dstptr, REL_CHILD), objid);          /* Child: object being moved */
}

/**
 * Removes an object from its parent (opcode: remove_obj)
 */
void op_remove_obj(void) {
    const zWord objid = G->operands[0];
    zByte *objptr = z_get_obj_ptr(objid);
    
    z_unparent_obj(objid);  /* Take object out of tree */
    
    /* Clear object relationships */
    z_field_set(z_obj_field(objptr, REL_PARENT), 0);   /* No parent */
    z_field_set(z_obj_field(objptr, REL_SIBLING), 0);  /* No sibling */
}

/**
 * Sets a property value on an object (opcode: put_prop)
 */
void op_put_prop(void) {
    const zWord objid = G->operands[0];
    const zWord propid = G->operands[1];
    const zWord value = G->operands[2];
    zByte size = 0;
    zByte *ptr = z_get_obj_property(objid, propid, &size);
    
    if (!ptr)
        G->die("Missing property obj=%X, prop=%X", (unsigned int)objid, (unsigned int)propid);
    else if (size == 1)
        *ptr = (value & 0xFF);
    else {
        Z_WRITE16(ptr, value);
    }
}

/**
 * Gets a property value from an object (opcode: get_prop)
 */
void op_get_prop(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord objid = G->operands[0];
    const zWord propid = G->operands[1];
    zWord result = 0;
    zByte size = 0;
    zByte *ptr = z_get_obj_property(objid, propid, &size);
    
    if (!ptr)
        result = z_get_default_property(propid);
    else if (size == 1)
        result = *ptr;
    else {
        result = Z_READ16(ptr);
    }
    
    Z_WRITE16(store, result);
}

/**
 * Gets the address of a property (opcode: get_prop_addr)
 */
void op_get_prop_addr(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord objid = G->operands[0];
    const zWord propid = G->operands[1];
    zByte *ptr = z_get_obj_property(objid, propid, NULL);
    zByte *base_ptr = zmem_get_ptr(&G->memory_state, 0, 0);
    const zWord result = ptr ? ((zWord)(ptr-base_ptr)) : 0;
    Z_WRITE16(store, result);
}

/**
 * Gets the length of a property (opcode: get_prop_len)
 */
void op_get_prop_len(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    zWord result;
    
    if (G->operands[0] == 0)
        result = 0;  /* Avoid bug in older Infocom games */
    else {
        const zWord offset = G->operands[0];
        result = z_prop_len_at(z_get_mem_ptr(offset));
    }
    
    Z_WRITE16(store, result);
}

/**
 * Gets the next property of an object (opcode: get_next_prop)
 */
void op_get_next_prop(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord objid = G->operands[0];
    const int firstProp = (G->operands[1] == 0);
    zWord result = 0;
    
    zByte *ptr = z_get_obj_property_table(objid);
    if (!ptr) {
        Z_WRITE16(store, 0);
        return;
    }
    
    if (firstProp) {
        /* Get the first property (highest property number) */
        zWord num;
        zByte size;
        if (*ptr == 0) {
            result = 0;
        } else {
            z_prop_header(ptr, &num, &size);
            result = num;
        }
    } else {
        /* Find the property after the requested one */
        const zWord search_propid = G->operands[1];
        
        while (1) {
            if (*ptr == 0) {  /* End of properties */
                result = 0;
                break;
            }
            
            zWord num;
            zByte size;
            ptr += z_prop_header(ptr, &num, &size);
            
            if (num == search_propid) {
                /* Found the property, now get the next one */
                ptr += size;  /* Skip the current property data */
                
                /* Check if there's another property after this one */
                if (*ptr == 0) {
                    result = 0;  /* No more properties */
                } else {
                    z_prop_header(ptr, &num, &size);
                    result = num;  /* Get next property ID */
                }
                break;
            } else {
                ptr += size;  /* Skip property data and continue search */
            }
        }
    }
    
    Z_WRITE16(store, result);
}

/**
 * Tests if an object is a child of another (opcode: jin)
 */
void op_jin(void) {
    const zWord objid = G->operands[0];
    const zWord parentid = G->operands[1];
    
    if (objid == 0) {
        return;  /* Handle Zork 1 edge case */
    }
    
    zByte *objptr = z_get_obj_ptr(objid);
    z_do_branch((z_field_get(z_obj_field(objptr, REL_PARENT)) == parentid) ? 1 : 0);
}

/**
 * Gets an object relation field value (parent, sibling, or child)
 * 
 * @param objid Object ID
 * @param which REL_PARENT, REL_SIBLING or REL_CHILD
 * @return Value in the relation field
 */
static zWord z_get_obj_relation(zWord objid, int which) {
    zByte *objptr = z_get_obj_ptr(objid);
    return z_field_get(z_obj_field(objptr, which));
}

/**
 * Gets an object's parent (opcode: get_parent)
 */
void op_get_parent(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord result = z_get_obj_relation(G->operands[0], REL_PARENT);
    Z_WRITE16(store, result);
}

/**
 * Gets an object's sibling (opcode: get_sibling)
 */
void op_get_sibling(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord result = z_get_obj_relation(G->operands[0], REL_SIBLING);
    Z_WRITE16(store, result);
    z_do_branch((result != 0) ? 1: 0);
}

/**
 * Gets an object's child (opcode: get_child)
 */
void op_get_child(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord result = z_get_obj_relation(G->operands[0], REL_CHILD);
    Z_WRITE16(store, result);
    z_do_branch((result != 0) ? 1: 0);
}

/**
 * Prints an object's name (opcode: print_obj)
 */
void op_print_obj(void) {
    const zByte *name = z_get_obj_name(G->operands[0]);
    if (!name) return;
    z_print_zscii(name, 0);
}
