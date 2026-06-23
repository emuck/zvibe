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
 * Implements object tree navigation and property access for Z-machine V3.
 *
 * **V3 Object Table Structure:**
 * - 31 property defaults (62 bytes)
 * - Up to 255 objects (9 bytes each)
 * - Each object: 4 bytes attributes + parent/sibling/child + properties pointer
 *
 * **Key Operations:**
 * - Tree navigation: parent, sibling, child
 * - Attribute access: test, set, clear (32 attributes per object)
 * - Property access: get, set, enumerate
 * - Object insertion/removal from tree
 */

#include "zvibe.h"

/* Object table constants for Version 3 */
#define Z3_OBJ_SIZE          9       /* Size of an object entry in Version 3 */
#define Z3_PROP_DEFAULTS     31      /* Number of property defaults */
#define Z3_ATTR_COUNT        32      /* Number of attributes in Version 3 */
#define Z3_MAX_OBJECTS       255     /* Maximum object count in Version 3 */

/* Object attribute and offset constants */
#define Z3_OBJ_ATTR_BYTES    4       /* Number of attribute bytes per object */
#define Z3_OBJ_PARENT_OFFSET 4       /* Offset to parent field */
#define Z3_OBJ_SIBLING_OFFSET 5      /* Offset to sibling field */
#define Z3_OBJ_CHILD_OFFSET  6       /* Offset to child field */
#define Z3_OBJ_PROP_OFFSET   7       /* Offset to properties field */

/**
 * Gets a pointer to an object in the object table
 * 
 * @param objid Object ID to lookup
 * @return Pointer to object data in story memory, or NULL for object 0
 */
zByte *z_get_obj_ptr(zWord objid) {
    if (objid == 0)
        return NULL;  /* Handle special case of object 0 */
        
    if (objid > Z3_MAX_OBJECTS)
        G->die("Invalid object id referenced");
        
    zByte *ptr = zmem_get_ptr(&G->memory_state, G->header.objtab_addr, 0);
    ptr += Z3_PROP_DEFAULTS * 2;            /* Skip properties defaults table */
    ptr += Z3_OBJ_SIZE * (objid-1);         /* Find object in table */
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
    const zWord parent = objptr[Z3_OBJ_PARENT_OFFSET];
    return parent ? z_get_obj_ptr(parent) : NULL;
}

static zByte *z_get_obj_property_table(zWord objid) {
    zByte *ptr = z_get_obj_ptr(objid);
    if (!ptr) return NULL;

    ptr += Z3_OBJ_PROP_OFFSET;
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
        zByte *ptr = parentptr + Z3_OBJ_CHILD_OFFSET;  /* Parent's child field */
        
        /* If not direct child, scan through siblings until we find it */
        while (*ptr != _objid) {
            ptr = z_get_obj_ptr(*ptr) + Z3_OBJ_SIBLING_OFFSET;
        }
        
        /* Replace with object's sibling */
        *ptr = *(objptr + Z3_OBJ_SIBLING_OFFSET);
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
        const zByte info = *ptr++;
        if (info == 0) break;              /* End of properties */
        
        const zWord num = (info & 0x1F);   /* 5 bits for prop id */
        const zByte size = ((info >> 5) & 0x7) + 1; /* 3 bits for size */
        
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
    
    ptr += Z3_OBJ_PROP_OFFSET;             /* Skip to properties address field */
    const zWord addr = Z_READ16(ptr);
    return zmem_get_ptr(&G->memory_state, addr + 1, 0);        /* +1 to skip z-char count */
}

/**
 * Gets a default property value
 * 
 * @param propid Property ID
 * @return Default value for property
 */
zWord z_get_default_property(zWord propid) {
    if (propid > Z3_PROP_DEFAULTS) {
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
    
    if (attrid >= Z3_ATTR_COUNT)
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
    
    if (attrid >= Z3_ATTR_COUNT)
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
    
    if (attrid >= Z3_ATTR_COUNT)
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
    *(objptr + Z3_OBJ_PARENT_OFFSET) = (zByte)dstid;             /* Parent: new destination */
    *(objptr + Z3_OBJ_SIBLING_OFFSET) = *(dstptr + Z3_OBJ_CHILD_OFFSET);  /* Sibling: dest's old child */
    *(dstptr + Z3_OBJ_CHILD_OFFSET) = (zByte)objid;              /* Child: object being moved */
}

/**
 * Removes an object from its parent (opcode: remove_obj)
 */
void op_remove_obj(void) {
    const zWord objid = G->operands[0];
    zByte *objptr = z_get_obj_ptr(objid);
    
    z_unparent_obj(objid);  /* Take object out of tree */
    
    /* Clear object relationships */
    *(objptr + Z3_OBJ_PARENT_OFFSET) = 0;  /* No parent */
    *(objptr + Z3_OBJ_SIBLING_OFFSET) = 0;  /* No sibling */
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
        const zByte *ptr = z_get_mem_ptr(offset);
        const zByte info = ptr[-1];  /* Size field */
        result = ((info >> 5) & 0x7) + 1; /* 3 bits for size */
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
        const zByte info = *ptr;
        result = info & 0x1F;  /* 5 bits for prop id */
    } else {
        /* Find the property after the requested one */
        const zWord search_propid = G->operands[1];
        
        while (1) {
            const zByte info = *ptr++;
            if (info == 0) {  /* End of properties */
                result = 0;
                break;
            }
            
            const zWord num = (info & 0x1F);   /* 5 bits for prop id */
            const zByte size = ((info >> 5) & 0x7) + 1; /* 3 bits for size */
            
            if (num == search_propid) {
                /* Found the property, now get the next one */
                ptr += size;  /* Skip the current property data */
                
                /* Check if there's another property after this one */
                if (*ptr == 0) {
                    result = 0;  /* No more properties */
                } else {
                    result = *ptr & 0x1F;  /* Get next property ID */
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
    
    const zByte *objptr = z_get_obj_ptr(objid);
    z_do_branch((((zWord)objptr[Z3_OBJ_PARENT_OFFSET]) == parentid) ? 1 : 0);
}

/**
 * Gets an object relation field value (parent, sibling, or child)
 * 
 * @param objid Object ID
 * @param relationship Offset to the relation field
 * @return Value in the relation field
 */
static zWord z_get_obj_relation(zWord objid, zByte relationship) {
    const zByte *objptr = z_get_obj_ptr(objid);
    return objptr[relationship];
}

/**
 * Gets an object's parent (opcode: get_parent)
 */
void op_get_parent(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord result = z_get_obj_relation(G->operands[0], Z3_OBJ_PARENT_OFFSET);
    Z_WRITE16(store, result);
}

/**
 * Gets an object's sibling (opcode: get_sibling)
 */
void op_get_sibling(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord result = z_get_obj_relation(G->operands[0], Z3_OBJ_SIBLING_OFFSET);
    Z_WRITE16(store, result);
    z_do_branch((result != 0) ? 1: 0);
}

/**
 * Gets an object's child (opcode: get_child)
 */
void op_get_child(void) {
    zByte *store = z_var_addr(z_read_pc_byte(), 1);
    const zWord result = z_get_obj_relation(G->operands[0], Z3_OBJ_CHILD_OFFSET);
    Z_WRITE16(store, result);
    z_do_branch((result != 0) ? 1: 0);
}

/**
 * Prints an object's name (opcode: print_obj)
 */
void op_print_obj(void) {
    const zByte *ptr = z_get_obj_ptr(G->operands[0]);
    if (!ptr) return;
    ptr += Z3_OBJ_PROP_OFFSET;           /* Skip to properties field */
    const zWord addr = Z_READ16(ptr);    /* Get property table */
    ptr = zmem_get_ptr(&G->memory_state, addr + 1, 0);
    if (!ptr) return;
    z_print_zscii(ptr, 0);
}
