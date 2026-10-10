# Выполненная проверка — 10 октября 2026

Это отчёт о реально выполненных локальных проверках. Проверка Windows → Ethernet → Mac на двух устройствах не выполнена; задача полноценного второго монитора не объявляется завершённой.

## Среда

- Upstream BetterCast: `7bc5667966906a608fe5fb8825e16ce822aa4cd4`.
- Локальный MacBook Pro `Mac15,6`, Apple M3 Pro, 12 CPU/18 GPU cores, 18 GB памяти.
- macOS 27.0.1, Xcode 27.0. Встроенный экран 3024×1964; `NSScreen.maximumFramesPerSecond` возвращает 120. Мгновенная частота ProMotion не измерена.
- Qt 6.11.2, FFmpeg 9.0.2. CMake/Qt добавлены как локальные инструменты сборки; ничего не установлено на Windows.
- Активного выделенного Ethernet IPv4 и подключённого Windows-ПК нет. Модель адаптера/его согласованная скорость, ОС/GPU Windows неизвестны.

## Реализовано

Выделенный интерфейс/IP, ручной адрес, TLS и certificate pin, подтверждение первого IP, согласование протокола/режима, фактические endpoints, ограниченные очереди, восстановление с IDR, отдельные метрики, сохранение настроек, 60/120 cadence, аппаратный decoder и display link с последним кадром. Интеграция VDD переиспользует установленный монитор и проверяет поддерживаемый mode/extended topology через Windows API. Подробности в [инструкции](../ethernet-second-display.md).

## Собрано

| Проверка | Результат |
|---|---|
| Неизменённый `swift build --product BetterCastSender` | PASS — baseline собирается |
| Изменённый Swift target, debug и release | PASS |
| `LegacyMacReceiver`, release с deployment target macOS 10.15 | PASS — собран на текущем SDK, запуск на старой ОС не проверен |
| `BetterCast-Ethernet.app`, arm64 | PASS — локальная ad-hoc подпись и `codesign --verify --deep --strict` |
| C++/Qt executable с `ENABLE_SENDER=ON`, **на macOS** | PASS — общие transport/encoder/controller/UI; Windows capture/VDD API ветки не компилировались |
| Windows EXE / installer | **НЕ СОБРАНО** |
| Windows VDD driver / подпись / установка | **НЕ ВЫПОЛНЕНО** |

Предупреждения о deprecated AVSampleBufferDisplayLayer/CoreVideo API для старых macOS сохраняются, как и предупреждение существующего отправителя об exhaustive switch ScreenCaptureKit (новый `.microphone` case SDK). Нет ошибок компиляции проверенных targets. Локальная подпись Mac app не является Developer ID/notarization.

## Проверено автоматически

Команда: `BETTERCAST_TEST_TRANSPORT=1 bash scripts/test-ethernet.sh`. Дополнительно выполнены CMake build, shell syntax checks и `git diff --check`.

| Тест | Реальный результат |
|---|---|
| Length framing, invalid/oversized/zero headers, version/FPS/codec mismatch | PASS, в том числе 10 000 детерминированных malformed-header cases |
| 240 движущихся H.264 кадров из fixture 1920×1080, 120 FPS | PASS: 240/240 декодированы аппаратным VideoToolbox |
| Однослотовый mailbox без окна/scanout | PASS: 1 pending, 239 заменённых декодированных кадров; ни одного сжатого reference frame не пропущено |
| Локальный C++ FFmpeg encoder | PASS: libx264, 640×360@60, 60 AVCC payloads, 2 IDR с SPS/PPS; odd dimensions отклонены |
| H.264 framing / extradata | PASS: смешанные Annex-B start codes, полный последний PPS, avcC с 2/4-byte length prefixes, отклонение повреждённых длин |
| 120 без аппаратного Windows codec на Mac test host | PASS: нет молчаливого fallback на libx264 |
| Qt TLS client → реальный Swift NWListener/VideoToolbox через loopback | PASS: 240 payloads с тактом 120 FPS; все 240 кадров декодированы |
| Разрыв и переподключение того же тестового транспорта | PASS: новый version/format handshake и начало с SPS/PPS/IDR; 599 кадров суммарно в двух полных и одном прерванном сеансах (из 600 посланных до намеренного abort) |
| Неверный SHA-256 сертификата | PASS: connection не разрешён, video не отправлено |
| Недоступный выбранный интерфейс/IP | PASS: connection не разрешён, fallback не выполнен |
| Слишком большой payload / очередь | PASS: bounded policy сбрасывает целый stream; Stop отменяет retry |
| Таймеры retry после Stop | PASS — ожидание дольше интервала retry не создаёт новое соединение |

Пример batch-decode результата: 240/240 1080p, VideoToolbox hardware, около 0,67 с (около 359 FPS batch throughput). Это тест пропускной способности декодера, **не фактический видеопоток/вывод 359 FPS** и не E2E. Пример TCP RTT loopback — около 1 мс; это не Ethernet RTT и не задержка до экрана. Значения зависят от нагрузки локального теста.

Для integration harness разрешение loopback и автоподтверждение включаются только compile flag `BETTERCAST_ETHERNET_TEST`; в production app/client этот flag не используется. В течение этих tests video window не выводилось на экран; presentation 120 FPS не проверена.

## Проверено на двух реальных устройствах

**Ничего из перечисленного ниже пока не подтверждено.**

- Загрузка принятого Windows подписанного VDD и отдельный extended monitor.
- Физические режимы VDD 1080p60/120, перетаскивание Windows окон, DPI, курсор DXGI/GDI.
- Работа NVENC/AMF/QSV на конкретном Windows GPU, реальные capture/encode 120 FPS.
- Только Ethernet при активном Wi-Fi, Internet-off test, физический cable unplug/replug.
- 120 receive/decode/new-frame presentation при движении на Mac, actual scanout/ProMotion, перенос окна на 60 Hz экран.
- Число мониторов после перезапусков, стабильность десятиминутной прокрутки/нагрузки.
- E2E median/p95, CPU/GPU, читаемость текста и сравнительная таблица 60 против 120.

Следующие действия: выполнить приведённую в инструкции Windows сборку, проверить подпись/установку одного VDD и заполнить аппаратный протокол 60/120 на двух устройствах. При блокировке подписи задача виртуального второго экрана остаётся открытой; независимо можно проверить передачу уже существующего монитора.
