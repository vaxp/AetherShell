# دليل المطورين: إنشاء إضافات لشريط البانل (venom-panel)

يوفر **Venom Panel** نظام إضافات ديناميكية مبني على مكتبات مشتركة (`.so`) وملف إعدادات مرئي يتحكم بترتيب وحجم كل عنصر في البانل.

---

## 0. نظام تخطيط البانل (`panel.conf`)

يقرأ `venom-panel` ملف الإعدادات `~/.config/venom/panel.conf` عند كل تشغيل. إذا لم يكن موجوداً، يُنشئه تلقائياً بالتخطيط الافتراضي.

### أنواع العناصر

| النوع (`type`) | الوظيفة |
|---|---|
| `plugin` | إضافة خارجية `.so` من `~/.config/venom/panel-plugins/` |
| `builtin` | عنصر مدمج (انظر الجدول أدناه) |
| `spacer` | مسافة شفافة تمتد وتملأ الفراغ المتبقي |
| `separator` | خط فاصل عمودي رفيع |

### العناصر المدمجة (`type=builtin`)

| الاسم (`name`) | الوصف |
|---|---|
| `tray` | System Tray (SNI Icons) + زر إيقاف التسجيل |
| `power` | بروفايل الطاقة + قائمة الإجراءات |
| `system-icons` | أيقونات WiFi / بطارية / صوت |
| `clock` | الساعة |
| `control-center` | زر فتح مركز التحكم |

### مثال كامل على `panel.conf`

```ini
# التخطيط الافتراضي — عدّل وأعد تشغيل البانل لتطبيق التغييرات
# الترتيب من الأعلى إلى الأسفل = من اليسار إلى اليمين

[item]
type=plugin
file=launcher.so
expand=false
padding=4

[item]
type=spacer
expand=true
padding=0

[item]
type=builtin
name=tray
expand=false
padding=4

[item]
type=separator

[item]
type=builtin
name=power
expand=false
padding=2

[item]
type=builtin
name=clock
expand=false
padding=4

[item]
type=separator

[item]
type=builtin
name=control-center
expand=false
padding=2
```

> **ملاحظة:** `expand=true` على عنصر `spacer` يجعله يدفع كل ما بعده نحو اليمين، مثل ما يفعله الـ Separator الموسّع في XFCE.

---


---

## 1. الواجهة البرمجية (API)

يجب تضمين الملف `venom-panel-plugin-api.h` في مشروعك:

```c
#include <gtk/gtk.h>
#include "venom-panel-plugin-api.h"
```

### هيكل الإضافة (`VenomPanelPluginAPI` / `VenomPanelPluginAPIv2`)
يتم تحديد الترتيب بالكامل عبر الإعدادات، لكن الإضافة تستطيع تمرير **تلميحات** (Hints) للبانل عن شكلها المفضل.

> **مهم:** يُفضّل استخدام API v2 عبر تصدير الدالة `venom_panel_plugin_init_v2` لأنها تضيف **تحقق من نسخة الـ ABI** وخيار `destroy_widget` لتنظيف المؤقتات/الموارد عند إعادة تحميل البانل.

```c
typedef struct {
    const char *name;          /* اسم الإضافة                             */
    const char *description;   /* وصف مختصر                              */
    const char *author;        /* اسم المطور                              */
    VenomPluginZone zone;      /* تلميح: يسار/وسط/يمين                   */
    int priority;              /* تلميح: أولوية الترتيب ضمن المنطقة      */
    gboolean    expand;        /* هل تتمدد الإضافة لملء الفراغ؟ (Hint)    */
    int         padding;       /* مسافة الحواف المفضلة بالبكسل (Hint)    */
    GtkWidget* (*create_widget)(void); /* دالة بناء الواجهة                  */
} VenomPanelPluginAPI;
```

**API v2 (الموصى به):**

```c
typedef struct {
    uint32_t api_version;      /* VENOM_PANEL_PLUGIN_API_VERSION */
    size_t   struct_size;      /* sizeof(VenomPanelPluginAPIv2)  */
    /* نفس الحقول أعلاه... */
    GtkWidget* (*create_widget)(void);
    void (*destroy_widget)(GtkWidget *widget); /* اختياري */
} VenomPanelPluginAPIv2;
```

---

## 2. كتابة إضافة من الصفر

**مثال: إضافة تعرض اسم المستخدم (`my-username.c`)**

```c
#include <gtk/gtk.h>
#include <stdlib.h>
#include <unistd.h>
#include "venom-panel-plugin-api.h"

static GtkWidget* create_username_widget(void) {
    /* الحصول على اسم المستخدم الحالي */
    const char *user = g_get_user_name();
    char label_text[64];
    snprintf(label_text, sizeof(label_text), "👤 %s", user ? user : "user");
    
    GtkWidget *label = gtk_label_new(label_text);
    
    /* تنسيق CSS */
    GtkCssProvider *css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(css,
        "label { color: #ecf0f1; font-weight: bold; padding: 0 8px; }", -1, NULL);
    gtk_style_context_add_provider(
        gtk_widget_get_style_context(label),
        GTK_STYLE_PROVIDER(css), 800);
    g_object_unref(css);
    
    gtk_widget_show_all(label);
    return label;
}

/* الدالة الأساسية (v1) — مدعومة للتوافق */
VenomPanelPluginAPI* venom_panel_plugin_init(void) {
    static VenomPanelPluginAPI api;
    api.name          = "Username Display";
    api.description   = "Shows the current user name.";
    api.author        = "You";
    api.expand        = FALSE;    /* لا تتمدد افتراضياً */
    api.padding       = 4;        /* 4 بكسل حواف */
    api.create_widget = create_username_widget;
    return &api;
}

/* الدالة الموصى بها (v2) */
VenomPanelPluginAPIv2* venom_panel_plugin_init_v2(void) {
    static VenomPanelPluginAPIv2 api;
    api.api_version   = VENOM_PANEL_PLUGIN_API_VERSION;
    api.struct_size   = sizeof(VenomPanelPluginAPIv2);
    api.name          = "Username Display";
    api.description   = "Shows the current user name.";
    api.author        = "You";
    api.zone          = VENOM_PLUGIN_ZONE_LEFT;
    api.priority      = 0;
    api.expand        = FALSE;
    api.padding       = 4;
    api.create_widget = create_username_widget;
    api.destroy_widget = NULL; /* ضعها إذا لديك مؤقتات/موارد للتنظيف */
    return &api;
}
```

> **حماية البانل (Graceful Fallback):** إذا أرجعت دالة `create_widget` قيمة `NULL` أو حدث خطأ أثناء تحميل مكتبتك (`.so`)، **لن ينهار البانل**. بدلاً من ذلك، سيقوم البانل بوضع أيقونة تحذير حمراء ⚠️ مكان إضافتك ليخبر المستخدم بوجود مشكلة، ويستمر البانل في عرض باقي العناصر بشكل طبيعي.

---

## 3. التجميع (Compiling)

### يدوياً:
```bash
gcc -shared -fPIC -o my-username.so my-username.c \
    $(pkg-config --cflags --libs gtk+-3.0) \
    -I/path/to/venom-panel/include
```

### داخل مجلد مصدر المشروع:
ضع ملفك في `src/panel-plugins/` وشغّل:
```bash
make panel-plugins
```
سيقوم `Makefile` تلقائياً بتجميع وتثبيت جميع الإضافات في المجلد الصحيح.

---

## 4. مسار التثبيت

ضع ملف `.so` في:
```
~/.config/venom/panel-plugins/
```
أي: `/home/YOUR_USERNAME/.config/venom/panel-plugins/`

سيقرأ `venom-panel` هذا المجلد **تلقائياً** عند كل عملية تشغيل.

> **ملاحظة أمنية:** قيمة `file=` داخل `panel.conf` يجب أن تكون **اسم ملف فقط** (basename) وتنتهي بـ `.so` (مثل `launcher.so`). لن يتم قبول مسارات تحتوي `/` أو `..` أو روابط رمزية (symlinks).

---

## 5. إدارة البانل (Edit Mode) وتحديث الواجهة

على عكس الأنظمة القديمة، **لم تعد بحاجة لكتابة الكود لتغيير ترتيب العناصر.** البانل يوفر الآن وضع تعديل مرئي (Edit Mode) متطور:

1. **إعدادات البانل:** انقر بزر الماوس الأيمن على أي مساحة فارغة في البانل واختَر **"Panel Preferences..."**.
2. **الترتيب:** ستظهر لك نافذة يمكنك منها نقل إضافتك (`username.so`) للأعلى أو الأسفل وتغيير مكانها الفعلي على الشاشة.
3. **التحديث المباشر (Live Reload):** بمجرد أن تحفظ التعديلات في واجهة الإعدادات، سيقوم `venom-panel-settings` بإرسال إشارة للبانل، فيقوم بإعادة ترتيب العناصر فوراً أمام عينيك دون إطفاء البرنامج.

## 6. نصائح متقدمة

- **التحديثات الدورية:** استخدم `g_timeout_add()` داخل `create_widget` لتحديث واجهة إضافتك في الخلفية (مثل تغيير الوقت أو البيانات المباشرة).
- **التلميحات ضد الإعدادات:** القيم المُرجعة في `api.expand` و `api.padding` يمكن دائماً تخطيها وكسرها من قِبل المستخدم عبر `panel.conf` إذا رغب في ذلك.
- **مثال حي:** راجع الكود المصدري في `src/panel-plugins/launcher.c` لتوضيح كيفية ربط الأزرار وتشغيل البرامج كإضافة خارجية.
