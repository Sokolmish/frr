// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Debugging utilities.
 * Copyright (C) 2018  Cumulus Networks, Inc.
 * Quentin Young
 */
#ifndef _FRRDEBUG_H
#define _FRRDEBUG_H

#include <zebra.h>
#include "command.h"
#include "frratomic.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Debugging modes.
 *
 * FRR's convention is that a debug statement issued under the vty CONFIG_NODE
 * persists to the config file, whereas the same debug statement issued from
 * the ENABLE_NODE only persists for the current session. These are mapped to
 * DEBUG_MODE_CONF and DEBUG_MODE_TERM respectively.
 *
 * They are not mutually exclusive and are placed in the MSB of the flags
 * field in a debugging record.
 */
#define DEBUG_MODE_TERM 0x01000000
#define DEBUG_MODE_CONF 0x02000000
#define DEBUG_MODE_ALL (DEBUG_MODE_TERM | DEBUG_MODE_CONF)
#define DEBUG_MODE_NONE 0x00000000
#define DEBUG_OPT_ALL 0x00FFFFFF
#define DEBUG_OPT_NONE 0x00000000


PREDECL_LIST(debug_list);
/*
 * Debugging record.
 *
 * All operations on this record exposed in this header are MT-safe.
 *
 * flags
 *    A bitfield with the following format (bytes high to low)
 *    - [0] Debugging mode field (MSB)  | Mode
 *    - [1] Arbitrary flag field        | Option
 *    - [2] Arbitrary flag field        | Option
 *    - [3] Arbitrary flag field (LSB)  | Option
 *
 *              ALL THESE BYTES ARE YOURS - EXCEPT MODE.
 *                      ATTEMPT NO BIT OPS THERE.
 *
 *    The MSB of this field determines the debug mode, Use the DEBUG_MODE*
 *    macros to manipulate this byte.
 *
 *    The low 3 bytes of this field may be used to store arbitrary information.
 *    Usually they are used to store flags that tune how detailed the logging
 *    for a particular debug record is. Use the DEBUG_OPT* macros to manipulate
 *    those bytes.
 *
 *    All operations performed on this field should be done using the macros
 *    later in this header file. They are guaranteed to be atomic operations
 *    with respect to this field. Using anything except the macros to
 *    manipulate the flags field in a multithreaded environment results in
 *    undefined behavior.
 *
 * conf
 *    The configuration string that will be written to the config file.
 *
 * desc
 *    Human-readable description of this debugging record.
 */
struct debug {
	atomic_uint_fast32_t flags;
	const char *conf;
	const char *desc;

	struct debug_list_item item;
};

/*
 * Check if a mode is set for a debug.
 *
 * MT-Safe
 */
#define DEBUG_MODE_CHECK(name, mode)                                           \
	CHECK_FLAG_ATOMIC(&(name)->flags, (mode)&DEBUG_MODE_ALL)

/*
 * Check if an option bit is set for a debug.
 *
 * MT-Safe
 */
#define DEBUG_OPT_CHECK(name, opt)                                             \
	CHECK_FLAG_ATOMIC(&(name)->flags, (opt)&DEBUG_OPT_ALL)

/*
 * Check if bits are set for a debug.
 *
 * MT-Safe
 */
#define DEBUG_FLAGS_CHECK(name, fl) CHECK_FLAG_ATOMIC(&(name)->flags, (fl))

/*
 * Set modes on a debug.
 *
 * MT-Safe
 */
#define DEBUG_MODE_SET(name, mode, onoff)                                      \
	do {                                                                   \
		if (onoff)                                                     \
			SET_FLAG_ATOMIC(&(name)->flags,                        \
					(mode)&DEBUG_MODE_ALL);                \
		else                                                           \
			UNSET_FLAG_ATOMIC(&(name)->flags,                      \
					  (mode)&DEBUG_MODE_ALL);              \
	} while (0)

/* Convenience macros for specific set operations. */
#define DEBUG_MODE_ON(name, mode) DEBUG_MODE_SET(name, mode, true)
#define DEBUG_MODE_OFF(name, mode) DEBUG_MODE_SET(name, mode, false)

/*
 * Set options on a debug.
 *
 * MT-Safe
 */
#define DEBUG_OPT_SET(name, opt, onoff)                                        \
	do {                                                                   \
		if (onoff)                                                     \
			SET_FLAG_ATOMIC(&(name)->flags, (opt)&DEBUG_OPT_ALL);  \
		else                                                           \
			UNSET_FLAG_ATOMIC(&(name)->flags,                      \
					  (opt)&DEBUG_OPT_ALL);                \
	} while (0)

/* Convenience macros for specific set operations. */
#define DEBUG_OPT_ON(name, opt) DEBUG_OPT_SET(name, opt, true)
#define DEBUG_OPT_OFF(name, opt) DEBUG_OPT_SET(name, opt, true)

/*
 * Set bits on a debug.
 *
 * MT-Safe
 */
#define DEBUG_FLAGS_SET(name, fl, onoff)                                       \
	do {                                                                   \
		if (onoff)                                                     \
			SET_FLAG_ATOMIC(&(name)->flags, (fl));                 \
		else                                                           \
			UNSET_FLAG_ATOMIC(&(name)->flags, (fl));               \
	} while (0)

/* Convenience macros for specific set operations. */
#define DEBUG_FLAGS_ON(name, fl) DEBUG_FLAGS_SET(&(name)->flags, (type), true)
#define DEBUG_FLAGS_OFF(name, fl) DEBUG_FLAGS_SET(&(name)->flags, (type), false)

/*
 * Unset all modes and options on a debug.
 *
 * MT-Safe
 */
#define DEBUG_CLEAR(name) RESET_FLAG_ATOMIC(&(name)->flags)

/*
 * Set all modes and options on a debug.
 *
 * MT-Safe
 */
#define DEBUG_ON(name)                                                         \
	SET_FLAG_ATOMIC(&(name)->flags, DEBUG_MODE_ALL | DEBUG_OPT_ALL)

/*
 * Map a vty node to the correct debugging mode flags. FRR behaves such that a
 * debug statement issued under the config node persists to the config file,
 * whereas the same debug statement issued from the enable node only persists
 * for the current session.
 *
 * MT-Safe
 */
#define DEBUG_NODE2MODE(vtynode)                                               \
	(((vtynode) == CONFIG_NODE) ? DEBUG_MODE_ALL : DEBUG_MODE_TERM)

/*
 * Debug at the given level to the default logging destination.
 *
 * MT-Safe
 */
#define DEBUG(level, name, fmt, ...)                                           \
	do {                                                                   \
		if (DEBUG_MODE_CHECK(name, DEBUG_MODE_ALL))                    \
			zlog_##level(fmt, ##__VA_ARGS__);                      \
	} while (0)

/* Convenience macros for the various levels. */
#define DEBUGE(name, fmt, ...) DEBUG(err, name, fmt, ##__VA_ARGS__)
#define DEBUGW(name, fmt, ...) DEBUG(warn, name, fmt, ##__VA_ARGS__)
#define DEBUGI(name, fmt, ...) DEBUG(info, name, fmt, ##__VA_ARGS__)
#define DEBUGN(name, fmt, ...) DEBUG(notice, name, fmt, ##__VA_ARGS__)
#define DEBUGD(name, fmt, ...) DEBUG(debug, name, fmt, ##__VA_ARGS__)

/* Show current debugging status. */
void debug_status_write(struct vty *vty);

/*
 * Register a debug item.
 */
void debug_install(struct debug *debug);

/*
 * Initialize debugging.
 * Should only be called by libfrr.
 */
void debug_init(void);

#ifdef __cplusplus
}
#endif

#endif /* _FRRDEBUG_H */
