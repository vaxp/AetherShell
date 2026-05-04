# venom-panel

بانل سطح مكتب خفيف لـ vaxp-os مبني على GTK3، يدعم system tray، إدارة الطاقة، الساعة، ومركز التحكم.

---

## المتطلبات

### تثبيت الحزم المطلوبة للبناء

```bash
sudo apt-get install -y \
    gcc \
    make \
    libgtk-3-dev \
    libglib2.0-dev \
    libgio-cil-dev \
    libnm-dev \
    libpulse-dev \
    libx11-dev
```

---

## البناء

```bash
# بناء المشروع
make

# تنظيف ملفات البناء والبدء من جديد
make clean && make
```

---

## التشغيل

```bash
./venom-panel
```

أو عبر السكريبت المرفق:

```bash
./run.sh
```

---

## الميزات

- **System Tray** – دعم SNI (StatusNotifierItem)
- **مركز التحكم** – WiFi، Bluetooth، Ethernet، Do Not Disturb، صوت، إضاءة، تصوير الشاشة
- **إدارة الطاقة** – تبديل بروفايل الطاقة، إيقاف التشغيل، إعادة التشغيل، الإسبات، قفل الشاشة
- **الساعة** – عرض الوقت في شريط البانل
- **Responsive** – يُعيد حساب حجمه تلقائياً عند تغيير دقة الشاشة أو توصيل/فصل شاشة خارجية
