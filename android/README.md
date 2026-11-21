# NX Torrent Player — Android / Android TV

Официальный Android-клиент **NX Torrent Player** для Android TV, ТВ-приставок (Google TV, Xiaomi Mi Box, realme stick и др.) и мобильных устройств (смартфоны, планшеты).

---

## 📱 Особенности платформы

- **Универсальное приложение**:
  - Полная поддержка **Android TV / Google TV**: баннер на главном экране (Leanback), навигация пультом ДУ (D-pad), медиа-клавиши.
  - Поддержка смартфонов и планшетов: сенсорное управление (тапы, жесты, прокрутка).
  - Полноэкранный режим без системных панелей (Immersive Sticky Mode) и защита от засыпания экрана во время воспроизведения (`FLAG_KEEP_SCREEN_ON`).
- **Стриминг торрентов на лету**:
  - Встроенный BitTorrent-движок на C/C++ (`engine/`): прямое чтение кусков через сокеты POSIX.
  - Поддержка TorrServer и Jackett.
- **Облачная синхронизация**:
  - Встроенная синхронизация закладок и истории с **Google Drive** (вход по QR-коду / коду устройства) и **WebDAV** (Яндекс Диск, Mail.ru, Nextcloud).
- **Поддерживаемые архитектуры**:
  - `arm64-v8a` (современные смартфоны, планшеты и 64-битные приставки);
  - `armeabi-v7a` (бюджетные и 32-битные ТВ-боксы).

---

## 🛠️ Сборка проекта

### Требования
- **JDK 17** (или JDK из состава Android Studio);
- **Android SDK** (API 21 - 34);
- **Android NDK** (r22+ или r25+);
- **CMake** 3.16+.

### Сборка из командной строки

```bash
cd android

# Сборка Debug APK:
./gradlew assembleDebug

# Сборка Release APK:
./gradlew assembleRelease
```

Готовые APK-файлы будут созданы в:
`android/app/build/outputs/apk/release/`

### Установка на устройство через ADB

```bash
adb install android/app/build/outputs/apk/release/app-release.apk
```
