/*
 * ═══════════════════════════════════════════════════════════════════════════
 * 🔦 Vaxp Basilisk - basilisk Window (Cairo-Drawn)
 * ═══════════════════════════════════════════════════════════════════════════
 * النافذة الرئيسية الجديدة: شريط أفقي مرسوم بالكامل بـ Cairo
 * يحتوي على Search Bar + أيقونات الأوضاع الدائرية
 * ═══════════════════════════════════════════════════════════════════════════
 */

#ifndef basilisk_WINDOW_H
#define basilisk_WINDOW_H

#include "basilisk.h"
#include <cairo/cairo.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * أوضاع النافذة
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    SPOT_MODE_SEARCH = 0,   /* البحث العام (افتراضي) */
    SPOT_MODE_APPS,         /* مشغّل التطبيقات (grid) */
    SPOT_MODE_AI,           /* AI Chat */
    SPOT_MODE_COMMANDS,     /* الأوامر السريعة */
    SPOT_MODE_COUNT
} basiliskMode;

/* ═══════════════════════════════════════════════════════════════════════════
 * API العلنية
 * ═══════════════════════════════════════════════════════════════════════════ */

void basilisk_init(void);
void basilisk_show(void);
void basilisk_hide(void);
void basilisk_toggle(void);

/* إعادة رسم النافذة (عند تغيير الحالة) */
void basilisk_redraw(void);

#endif /* basilisk_WINDOW_H */
