/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#ifndef _RTW88_COMPAT_AVERAGE_H
#define _RTW88_COMPAT_AVERAGE_H

#include "types.h"

/*
 * Exponentially Weighted Moving Average (EWMA).
 *
 * exp18: match Linux include/linux/average.h semantics exactly for rtw88.
 * _weight_rcp is the reciprocal weight (2, 4, 16, ...), NOT a shift count.
 * The old compatibility shim used it directly as a shift.  rtw88 declares
 * DECLARE_EWMA(rssi, 10, 16), so the old code evaluated precision-weight as
 * 10-16 and corrupted the RSSI state that is fed back into firmware RA.
 *
 * rtw88 uses constant power-of-two weights. __builtin_ctzl(weight) is the
 * kernel-port equivalent of Linux ilog2(weight) for those constants.
 */
#define DECLARE_EWMA(name, _precision, _weight_rcp)                       \
    struct ewma_##name {                                                   \
        unsigned long internal;                                            \
    };                                                                     \
    static inline void ewma_##name##_init(struct ewma_##name *e)          \
    {                                                                      \
        e->internal = 0;                                                   \
    }                                                                      \
    static inline unsigned long ewma_##name##_read(struct ewma_##name *e) \
    {                                                                      \
        return e->internal >> (_precision);                                \
    }                                                                      \
    static inline void ewma_##name##_add(struct ewma_##name *e,           \
                                          unsigned long val)               \
    {                                                                      \
        unsigned long internal = e->internal;                              \
        unsigned long weight_rcp =                                        \
            (unsigned long)__builtin_ctzl((unsigned long)(_weight_rcp));   \
        unsigned long precision = (unsigned long)(_precision);             \
        e->internal = internal ?                                           \
            (((internal << weight_rcp) - internal) +                       \
             (val << precision)) >> weight_rcp :                           \
            (val << precision);                                            \
    }

#endif /* _RTW88_COMPAT_AVERAGE_H */
