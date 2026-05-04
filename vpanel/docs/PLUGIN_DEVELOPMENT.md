# دليل المطورين: إنشاء إضافات لشريط البانل (vpanel)

يوفر **Venom Panel** نظام إضافات ديناميكية مبني على مكتبات مشتركة (`.so`) وملف إعدادات مرئي يتحكم بترتيب وشكل كل عنصر في البانل.

---

## 0. نظام تخطيط البانل (`panel.conf`)

يقرأ `vpanel` ملف `~/.config/venom/panel.conf` عند كل تشغيل أو عند استقبال إشارة `SIGUSR1` (live reload).

### أنواع العناصر

| النوع (`type`) | الوظيفة |
|---|---|
| `plugin` | إضافة خارجية `.so` من `~/.config/venom/panel-plugins/` |
| `builtin` | عنصر مدمج |
| `spacer` | مسافة شفافة تمتد |
| `separator` | خط فاصل عمودي |

### العناصر المدمجة (`type=builtin`)

| الاسم | الوصف |
|---|---|
| `tray` | System Tray (SNI) |
| `power` | بروفايل الطاقة |
| `system-icons` | WiFi / بطارية / صوت |
| `clock` | الساعة |
| `control-center` | مركز التحكم |
| `workspaces` | أزرار المساحات |
| `volume`, `mic`, `wifi`, `bluetooth` | مؤشرات منفصلة |

### الخصائص البصرية per-plugin (جديد في v3)

يمكنك إضافة خصائص بصرية مخصصة لأي بلغن مباشرةً في `panel.conf`:

```ini
[item]
type=plugin
file=sys-monitor.so
expand=false
padding=4

# ── خلفية ──────────────────────────────────
bg_color=#1a1a2e        # لون خلفية الحاوية (#rrggbb أو #rrggbbaa)
bg_alpha=0.90           # شفافية إضافية [0.0 – 1.0]
bg_gradient_end=#16213e # إذا ذُكر يصبح التدرج من bg_color إلى هذا اللون

# ── حدود ──────────────────────────────────
border_color=#3d5af1    # لون الحدود
border_width=1          # سُمك الحدود بالبكسل (0 = بلا)
border_radius=8         # تقريب الزوايا بالبكسل
```

> **ملاحظة:** قيم `panel.conf` تتغلب دائماً على القيم الافتراضية التي يعرفها البلغن نفسه في `VenomPluginVisuals`.

### إعدادات مخصصة للبلغن (Extra Config Keys)

أي مفتاح لا يُعرَّف أعلاه يُرسَل مباشرةً للبلغن عبر `on_config_changed()`:

```ini
[item]
type=plugin
file=my-plugin.so
refresh_ms=500
show_labels=true
```

---

## 1. الواجهة البرمجية (API)

### API v3 (الموصى به — الإصدار الحالي)

```c
#include <gtk/gtk.h>
#include "vpanel-plugin-api.h"

VenomPanelPluginAPIv3* venom_panel_plugin_init_v3(void) {
    static VenomPanelPluginAPIv3 api = {
        .api_version  = VENOM_PANEL_PLUGIN_API_VERSION,  /* 3 */
        .struct_size  = sizeof(VenomPanelPluginAPIv3),

        /* الهوية */
        .name         = "My Plugin",
        .description  = "Does something cool.",
        .author       = "You",
        .version      = "1.0.0",
        .icon_name    = "application-x-executable",

        /* تلميحات التخطيط */
        .zone         = VENOM_PLUGIN_ZONE_LEFT,
        .expand       = FALSE,
        .padding      = 4,
        .min_width    = 0,
        .max_width    = -1,

        /* مظهر افتراضي (يمكن تخطيه من panel.conf) */
        .visuals = {
            .bg_type       = VENOM_PLUGIN_BG_SOLID,
            .bg_r = 0.1,  .bg_g = 0.1,  .bg_b = 0.18, .bg_a = 0.85,
            .border_enabled = TRUE,
            .border_r = 0.24, .border_g = 0.35, .border_b = 0.95,
            .border_a = 1.0,  .border_width = 1,  .border_radius = 6,
        },

        /* سلوك */
        .singleton    = FALSE,
        .watchdog_ms  = 3000,   /* يُلغى البلغن إذا لم تكتمل create_widget في 3 ث */

        /* دوال الحياة */
        .create_widget  = my_create_widget,
        .destroy_widget = my_destroy_widget,   /* NULL مسموح */

        /* قناة الإعدادات */
        .on_config_changed = my_on_config_changed,
        .get_config_schema = my_get_config_schema,
    };
    return &api;
}
```

#### تلخيص الحقول الجديدة في v3

| الحقل | النوع | الوصف |
|---|---|---|
| `version` | `const char*` | نسخة البلغن نفسه |
| `icon_name` | `const char*` | أيقونة الثيم تظهر في نافذة الإعدادات |
| `min_width` / `max_width` | `int` | قيود عرض الحاوية |
| `visuals` | `VenomPluginVisuals` | مظهر افتراضي (خلفية، حدود، ظل) |
| `singleton` | `gboolean` | لا يُحمَّل إلا نسخة واحدة |
| `watchdog_ms` | `guint` | timeout لـ `create_widget()` |
| `on_config_changed` | دالة | تُستدعى عند تغيير مفتاح إعداد |
| `get_config_schema` | دالة | قائمة الإعدادات المتاحة |

### VenomPluginVisuals — أنواع الخلفية

```c
typedef enum {
    VENOM_PLUGIN_BG_INHERIT      = 0,  /* مثل باقي البانل (الافتراضي) */
    VENOM_PLUGIN_BG_SOLID        = 1,  /* لون صلب */
    VENOM_PLUGIN_BG_TRANSPARENT  = 2,  /* شفاف تماماً */
    VENOM_PLUGIN_BG_GRADIENT     = 3,  /* تدرج أفقي */
} VenomPluginBackground;
```

**ماكرو مختصر:**
```c
/* خلفية صلبة دفعة واحدة */
.visuals = VENOM_VISUALS_SOLID(0.1, 0.1, 0.18, 0.9),

/* تدرج */
.visuals = VENOM_VISUALS_GRADIENT(0.1,0.1,0.18,1.0,  0.05,0.05,0.12,1.0),
```

### قناة الإعدادات

```c
/* يُستدعى عند تحميل البلغن وعند كل تغيير في panel.conf */
static void my_on_config_changed(GtkWidget *widget,
                                  const char *key,
                                  const char *value)
{
    if (g_strcmp0(key, "refresh_ms") == 0) {
        int ms = atoi(value);
        /* تطبيق الإعداد على الـ widget */
    }
}

/* البانل يستدعي هذه مرة واحدة لعرض الإعدادات في نافذة الإعدادات */
static void my_get_config_schema(const char ***keys, const char ***defaults)
{
    static const char *k[] = { "refresh_ms", "show_labels", NULL };
    static const char *d[] = { "1000",       "true",        NULL };
    *keys     = k;
    *defaults = d;
}
```

### API v2 (مدعوم للتوافق الخلفي)

```c
VenomPanelPluginAPIv2* venom_panel_plugin_init_v2(void) {
    static VenomPanelPluginAPIv2 api = {
        .api_version    = VENOM_PANEL_PLUGIN_API_VERSION_V2,   /* 2 */
        .struct_size    = sizeof(VenomPanelPluginAPIv2),
        .name           = "My Plugin",
        .create_widget  = my_create_widget,
        .destroy_widget = my_destroy_widget,
    };
    return &api;
}
```

---

## 2. نظام العزل (Plugin Isolation)

### كيف يعمل

```
vpanel (process)
├── GtkSocket [slot 0] ←── vpanel-plugin-host sys-monitor.so  (PID xxxx)
│                              └── GtkPlug → widget الحقيقي
├── GtkSocket [slot 1] ←── vpanel-plugin-host tasklist.so     (PID yyyy)
└── ...
```

- كل بلغن يعمل في **عملية فرعية مستقلة** (`vpanel-plugin-host`).
- إذا انهار البلغن: البانل يكتشف ذلك عبر `SIGCHLD`، يعرض أيقونة تحذير ⚠️، ويُعيد إطلاق البلغن تلقائياً بعد 2.5 ثانية.
- بعد **5 محاولات فاشلة**: يتوقف عن المحاولة ويعرض رسالة "Plugin Disabled".
- **على Wayland**: تشغيل داخل نفس العملية مع watchdog timer (لا يدعم GtkSocket).

### الحماية ضد التجميد

إذا عرّف البلغن `watchdog_ms > 0`، يُعطي البانل إنذاراً بـ `SIGALRM`؛ فإذا لم تعود `create_widget()` في الوقت المحدد يُعامَل البلغن كمعطوب.

---

## 3. التجميع والتثبيت

### تجميع بلغن

```bash
gcc -shared -fPIC -o my-plugin.so my-plugin.c \
    $(pkg-config --cflags --libs gtk+-3.0) \
    -I/path/to/vpanel/include
```

### داخل المشروع

```bash
# ضع my-plugin.c في src/panel-plugins/ ثم:
make panel-plugins
```

### مسار التثبيت

```
~/.config/venom/panel-plugins/my-plugin.so
```

---

## 4. نصائح متقدمة

- **التحديثات الدورية:** `g_timeout_add()` داخل `create_widget()`.
- **تنظيف المؤقتات:** استخدم `destroy_widget` دائماً إذا سجّلت `g_timeout_add` أو `g_signal_connect`.
- **Singleton:** إذا كان بلغنك يدير موردًا عالميًا (Bluetooth، Audio...) اضبط `.singleton = TRUE`.
- **Config Live:** `on_config_changed` يُستدعى مباشرةً عند SIGUSR1 reload — حافظ عليه سريعاً.
