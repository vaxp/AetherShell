#!/bin/bash

# الدليل المستهدف (الافتراضي هو الدليل الحالي)
TARGET_DIR="${1:-.}"

echo "جاري بدء استبدال النصوص والملفات في: $TARGET_DIR"

# 1. استبدال النصوص داخل الملفات (مع تجاهل ملف السكربت نفسه لتجنب المشاكل)
echo "جاري استبدال النصوص داخل الملفات..."
find "$TARGET_DIR" -type f -not -name "$(basename "$0")" -exec sed -i -e 's/VENOM/VAXP/g' -e 's/venom/vaxp/g' -e 's/Venom/Vaxp/g' {} + 2>/dev/null

# 2. إعادة تسمية الملفات والمجلدات (استخدام -depth لضمان تغيير أسماء الملفات قبل المجلدات التي تحتويها)
echo "جاري إعادة تسمية الملفات والمجلدات..."
find "$TARGET_DIR" -depth \( -name '*VENOM*' -o -name '*venom*' -o -name '*Venom*' \) | while read -r file; do
    dir=$(dirname "$file")
    base=$(basename "$file")
    
    # استبدال الكلمة مع الحفاظ على حالة الأحرف
    newbase=$(echo "$base" | sed -e 's/VENOM/VAXP/g' -e 's/venom/vaxp/g' -e 's/Venom/Vaxp/g')
    
    if [ "$base" != "$newbase" ]; then
        mv "$file" "$dir/$newbase"
        echo "تمت إعادة التسمية: $file -> $dir/$newbase"
    fi
done

echo "تمت العملية بنجاح!"
