# MicroPython внутри HackyLens: исследование и план реализации

Дата исследования: 2026-08-04. Фактические результаты обновлены 2026-08-09.

## Краткий вывод

Пункт 5 roadmap реализуем на текущем K210 и в текущей архитектуре HackyLens.
Рекомендуемая основа — официальный MicroPython 1.28.0 в режиме `embed`, а не
перенос MaixPy как второй прошивки. VM следует запускать как длительную задачу
на core 1; главный цикл, экран, кнопки, USB-UART и владение периферией должны
остаться на core 0. Аппаратные Python-bindings вызывают сервисы HackyLens через
ограниченный RPC/mailbox и никогда не обращаются к SDK-драйверам напрямую.

Скрипты следует хранить в littlefs в отдельном разделе внутренней NOR flash.
На исследованном SEN0305 установлен 16-МБ flash: оригинальный пакет использует
адреса вплоть до `0x00C6E000`. Поэтому безопасный предварительный кандидат для
пользовательского раздела — `0x00C70000..0x00FFFFFF` (3 735 552 байта, 912
секторов по 4 КБ). Включать раздел можно только после проверки JEDEC capacity;
на 8-МБ вариантах приложение должно сообщать `UNSUPPORTED_FLASH`, а не писать
за пределы микросхемы.

Первую версию надо разделить на три вертикальных этапа: надёжное хранилище и
протокол, минимальная VM с безопасной остановкой, затем bindings и IDE. Камеру,
KPU и SD нельзя включать в первый релиз bindings: у них более сложное владение,
большие буферы и асинхронный lifecycle.

## Исходный baseline до реализации пункта 5

- До добавления MicroPython полная прошивка собиралась в 1 312 632 байта. ELF
  содержал 2 415 168 байт `.bss`; его статические данные заканчивались около
  `0x8038E240`. Эти числа использовались только как исходная оценка бюджета;
  итоговые размеры приведены в разделе фактических результатов ниже.
- Прошивка уже имела feature-модули, `hk_app_t`, безопасный lifecycle входа и
  выхода, общий `core1_executor`, UART3 debug console и кольцевой терминал.
- Исходный `boot_flash` умел JEDEC ID, чтение, sector erase и page program, но
  ещё не имел достаточных для filesystem bounds/page checks, результата timeout,
  взаимного исключения и передачи ошибок наверх.
- Settings v4 занимают два сектора `0x007FE000` и `0x007FF000`; build guard не
  даёт образу пересечь первый слот. Их следует оставить без изменений.
- PC-соединение — UART3 через USB-serial мост, а не нативный USB device K210.
  Значит «USB-протокол» roadmap фактически является протоколом поверх уже
  работающего serial-порта.
- Исходный firmware package подтверждает 16-МБ раскладку: основной образ лежит
  с нуля, модели занимают области до конца `object_detect.bin` около
  `0x00C5BDE8`, а очистка служебных секторов заканчивается на `0x00C6E000`.

Локальные источники этих выводов: `tools/build_firmware.py`,
`firmware/src/storage/settings_store.c`, `firmware/src/drivers/boot_flash.c`,
`firmware/src/services/core1_executor.c`, `build/sdk-full/hackylens_full` и
`../unpacked/flash-list.json`.

## Выбор MicroPython

### Рекомендуется: upstream `embed`

Официальный MicroPython имеет Tier 2 port `embed`, который генерирует
самодостаточный набор `.c/.h` для встраивания в существующий C-проект. Его
пример принимает GC heap от хост-приложения, выполняет строки и затем вызывает
deinit. Это ровно нужная модель владения: HackyLens остаётся основной
прошивкой, а Python VM создаётся только на время работы приложения.

Зафиксировать в lock-файле:

- MicroPython `v1.28.0` и полный commit SHA;
- конфигурацию `mpconfigport.h`;
- сгенерированный список исходников или воспроизводимую команду `embed.mk`;
- версию `.mpy`, хотя v1 должен передавать исходные `.py`, чтобы IDE не зависела
  от bytecode ABI.

Фактическая конфигурация VM v1 (первоначальные оценки 256–512 КиБ heap и
32-КиБ Python stack после compile/link-аудита были уменьшены):

- `MICROPY_CONFIG_ROM_LEVEL_MINIMUM`, compiler, GC, exceptions, `sys`, `gc` и
  явно включённые builtins `min/max`;
- float single precision;
- без REPL, `_thread`, network, SSL/TLS, native/viper emitter, dynamic native
  modules, persistent-code loader, VFS imports и доступа к произвольной памяти;
- без стандартного `machine`: вместо него модуль `hackylens` с контролируемыми
  объектами;
- статический выровненный GC heap 128 КиБ, принадлежащий только core 1;
- Python stack limit 24 КиБ с 1-КиБ stack-check margin и лимит исходного файла
  65535 байт;
- heap, source и output buffers зарезервированы один раз и переиспользуются;
  `mp_embed_deinit` очищает VM-состояние, не создавая фрагментацию C heap между
  запусками.

В v1 поддерживается один исходный entrypoint и встроенные модули. Импорт других
пользовательских `.py` из littlefs надо добавить отдельным этапом через
ограниченный reader/import hook; нельзя случайно открывать VM весь flash block
device или SD.

Официальный минимальный embed-пример работает даже с 8-КБ heap, но это лишь
демонстрация API, а не реалистичный бюджет пользовательского приложения.
MicroPython использует отдельный Python heap, не совпадающий с C heap, поэтому
оба бюджета надо измерять раздельно.

### Почему не переносить MaixPy-v1 целиком

MaixPy-v1 доказывает, что MicroPython, KPU, камера, LCD и периферия работают на
K210. Но это законченная альтернативная платформа, основанная на старом fork
MicroPython/OpenMV и собственной сборочной системе. Репозиторий прямо описывает
её как MicroPython firmware для K210 и больше не поддерживает историческую
ветку. Перенос всего port вернёт в HackyLens вторую HAL, второе владение
камерой/LCD/UART и второй lifecycle.

MaixPy полезен только как справочник по K210-специфичным решениям: сборка
RISC-V, GC/stack collection, cache barriers и возможные реализации bindings.
Копировать следует лишь минимально необходимые фрагменты после отдельной
проверки лицензии каждого файла. Общий license MaixPy-v1 — MIT/Apache-2.0;
MicroPython/OpenMV части обозначены как MIT.

## Выполнение и безопасная остановка

### Размещение по ядрам

Синхронный `mp_embed_exec_*()` нельзя вызывать из `app.tick` на core 0:
бесконечный `while True` остановит кнопки, экран, debug protocol и системный
tick. VM должна быть одной job в существующем `core1_executor`.

Последовательность запуска:

1. Core 0 закрывает предыдущее приложение и проверяет, что core 1 свободен.
2. Storage читает выбранный `.py` в ограниченный буфер и проверяет размер.
3. Runtime подключает статически зарезервированный GC heap, создаёт resource
   ledger и публикует epoch запуска.
4. Core 1 фиксирует вершину именно своего стека, инициализирует VM и выполняет
   код.
5. Core 0 продолжает UI tick, обрабатывает mailbox bindings и выгружает stdout.
6. После обычного завершения или исключения core 1 вызывает deinit и публикует
   итог. Core 0 освобождает все leases и только затем разрешает выход в меню.

MicroPython и APRILTAG не работают одновременно: оба используют единственный
executor. Перед запуском Python следует дождаться завершения отложенного
APRILTAG destroy. В перспективе executor стоит дополнить owner ID и cancel
contract, не создавая второй entry point для core 1.

### Два уровня остановки

Кооперативная остановка — основной путь:

- atomic `stop_requested` и deadline проверяются через `MICROPY_VM_HOOK_LOOP`,
  `MICROPY_VM_HOOK_RETURN` и обе центральные точки `mp_iternext*`; pinned patch
  поэтому охватывает также native C-циклы `sum`, `min/max` и конструкторов;
- hook поднимает `KeyboardInterrupt` внутри VM;
- `time.sleep*` дробится на интервалы не более 10 мс и также проверяет stop;
- каждый C binding обязан иметь конечный timeout и точки отмены;
- native/viper запрещены, иначе Python-код сможет надолго уйти из bytecode VM.

Временной лимит должен быть политикой запуска, а не одним жёстким значением:
IDE может запросить лимит до установленного firmware maximum. В v1 значение `0`
выбирает безопасный default 30 секунд, maximum равен 300 секундам; кнопка STOP
доступна при любом лимите.

Аппаратный watchdog — только аварийный путь. Если core 1 не завершился через
2 секунды после STOP либо через 5 секунд после deadline, безопасно «убить поток»
и вернуться в меню невозможно: его C-код мог владеть ресурсом. Core 0 тогда
одноразово запускает WDT1 и ждёт аппаратного reset; обычные flash-операции под
watchdog не выполняются. Причина WDT1 сохраняется, firmware autostart
подавляется на этот boot, чтобы не получить boot loop; native-приложение само
никогда не запускает startup script при входе, а явный ручной RUN остаётся
доступен.

### Resource ledger

В реализованном v1 ресурсы имеют небольшой фиксированный dependency order, а
не универсальную динамическую таблицу: display lease берётся первым при
подготовке запуска, external UART/I2C и LED/RGB state отмечаются лениво при
первом использовании. Идемпотентный cleanup останавливает external transaction,
возобновляет connector service, восстанавливает настройки света и последним
снимает display overlay. Такой обратный порядок покрыт executable exact-once
тестами; независимость этих трёх групп не требует общего order stack. Python не
получает сырые указатели, номера FPIOA, SPI flash, watchdog или возможность
менять clocks. Ошибка, исключение, STOP и выход из приложения проходят через
одну cleanup state machine.

## Flash-хранилище

### Предлагаемая карта

| Диапазон | Размер | Назначение |
| --- | ---: | --- |
| `0x000000..image_end` | переменный | HackyLens firmware |
| `0x007FE000..0x007FFFFF` | 8 КБ | существующие settings slots |
| `0x00C70000..0x00FFFFFF` | 3 735 552 Б | littlefs scripts |

Диапазоны между образом, settings и `0x00C70000` не объявляются свободными:
они использовались моделями и splash оригинальной прошивки. Такое решение также
не заставляет firmware image расти до размера раздела.

Нужен versioned flash-layout descriptor в release metadata. Flasher по
умолчанию обновляет только firmware и сохраняет userfs; отдельные явные команды
`format-userfs`/`erase-userfs` могут стереть раздел. Полное восстановление
оригинального пакета не обязано сохранять Python-файлы.

Перед монтированием firmware обязана:

1. прочитать JEDEC ID;
2. вычислить capacity из density byte и сверить с allowlist протестированных
   чипов;
3. проверить, что все разделы выровнены и не пересекаются;
4. отключить userfs на capacity меньше 16 МБ;
5. никогда автоматически не форматировать повреждённую файловую систему без
   подтверждения пользователя или команды IDE.

### Почему littlefs

Littlefs рассчитан на NOR flash, имеет copy-on-write, устойчивость к сбою
питания, динамическое выравнивание износа, bounded RAM и атомарные операции
rename/remove. Это лучше самодельного append-only журнала, как только нужны
list/read/delete и несколько файлов. Код BSD-3-Clause совместим с MIT-проектом.

Начальный block config для проверки на реальной микросхеме:

- `read_size = 1` или подтверждённая минимальная гранулярность драйвера;
- `prog_size = 256`;
- `block_size = 4096`;
- `block_count = 912`;
- `cache_size = 256`, два статических cache buffer;
- `lookahead_size = 32` как стартовая точка, затем измерение;
- `block_cycles = 500` как стартовая точка, затем endurance review.

CRC littlefs защищает метаданные; transport дополнительно проверяет CRC32 всего
загруженного файла до атомарного commit. Upload идёт во временное имя
`.upload-<id>`, после `close/sync` файл перечитывается/проверяется и атомарно
переименовывается. Неоконченные временные файлы очищаются при следующем mount.

До подключения littlefs надо усилить `boot_flash`:

- checked range и page-boundary split;
- возврат `OK/TIMEOUT/IO/BOUNDS` вместо `void`;
- read-after-program там, где нужен transport-level verify;
- общий lock/owner для settings и littlefs;
- запрет erase/program из core 1;
- тест block device на RAM/file backend с fault injection после каждого
  program/erase шага.

## Serial protocol v1

Смешивать произвольный stdout с текущими строковыми `HK...` командами нельзя.
Совместимый переход:

1. В обычном line mode IDE отправляет `HKMPROTO 1`.
2. Устройство отвечает текстом `HKMPROTO 1 READY` и переключает UART3 в
   exclusive framed mode.
3. Все дальнейшие ответы, stdout и events идут только кадрами.
4. `SESSION_CLOSE`, disconnect timeout или reboot возвращает line mode.

Формат кадра: COBS payload + delimiter `0x00`. Декодированный header:

| Поле | Размер |
| --- | ---: |
| magic `HMPY` | 4 |
| version | 1 |
| type | 1 |
| flags | 2 |
| request_id | 4 |
| payload_len | 4 |
| payload | N, максимум 1024 в v1 |
| CRC32 header+payload | 4 |

COBS гарантирует однозначное восстановление границы по нулю; CRC32 обнаруживает
повреждение. `request_id` позволяет IDE сопоставлять ответы, а async events
используют ноль. Все integer — little-endian. Parser имеет фиксированный
буфер, отбрасывает oversized frame и восстанавливается на следующем delimiter.

Минимальные request/response:

- `HELLO` — protocol, firmware, board, flash profile, capabilities, limits;
- `LIST`, `STAT`, `READ`;
- `UPLOAD_BEGIN`, `UPLOAD_CHUNK`, `UPLOAD_COMMIT`, `UPLOAD_ABORT`;
- `DELETE`, `SET_STARTUP`, `FORMAT` с отдельным подтверждающим token;
- `RUN`, `STOP`, `STATUS`;
- events `STDOUT`, `STDERR`, `STATE`, `FILE_CHANGED`, `DROPPED`;
- `SESSION_CLOSE` и `PING`.

Каждый upload содержит ожидаемые size и CRC32. Chunks имеют offset, повторная
доставка того же chunk идемпотентна, пропуск/перекрытие возвращает ошибку.
stdout проходит через bounded 4-КиБ ring; при переполнении старые
байты не должны молча исчезать — отправляется `DROPPED(bytes)`.

## Bindings v1

Реализованный публичный API v1 сознательно оставлен плоским и небольшим:

```python
import hackylens as hl

if hl.button(hl.BUTTON_BACK):
    print("back")

hl.display_clear(0x0000)
hl.display_text(8, 8, "Hello", 0xffff)
hl.display_present()
hl.led(50)                  # 0..100
hl.rgb(255, 0, 102)         # каждый канал 0..255
hl.sleep_ms(100)
```

Namespaced objects, button edge events и нормализованный RGB `0..100` были
ранним вариантом дизайна и не являются контрактом v1. Канонический контракт
зафиксирован в `MICROPYTHON_API.md`.

Ограничения v1:

- `buttons`: level snapshot/read, без edge state и Python IRQ callbacks;
- `display`: bounded command buffer, clipping и один Python overlay; запись в
  LCD shadow/present выполняется core 0;
- `time`: ticks/sleep с wrap-safe semantics;
- LED: `0..100`; RGB: три канала `0..255`;
- UART/I2C: только внешний connector через существующие HAL/services, с
  exclusive lease (обычный external-link service на это время приостановлен),
  лимитом буфера и timeout; debug UART3 недоступен Python;
- STOP/BACK всегда имеет приоритет над пользовательским чтением кнопок.

Bindings v2 (camera, KPU, SD/files, vision results) следует проектировать после
стабилизации runtime. Они должны переиспользовать `camera_session`,
`ai_model_runtime`, `file_*` и `vision_result_service`, а не MaixPy API и не
сырые SDK calls. KPU сохраняет правило одной загруженной модели.

## Приложение на устройстве

Feature folder `firmware/src/apps/micropython/` должен иметь единственный
публичный `micropython_app.h` и включаться целиком через
`--disable-app micropython`. Внутри нужны controller, view, runtime adapter,
file model и config; littlefs/block device остаётся общей storage subsystem,
так как ей также пользуется IDE protocol. Но build manifest должен условно
убирать VM, littlefs и protocol целиком, когда MicroPython app отключён.

Минимальный UI:

- список `.py`, индикатор startup и свободного места;
- RUN/STOP, DELETE с подтверждением, SET/CLEAR STARTUP;
- read-only просмотр исходника с постраничным чтением без загрузки файла целиком;
- экран логов с follow/scroll и итоговым статусом;
- BACK во время запуска сначала запрашивает stop и остаётся на экране;
- выход в меню разрешён только после cleanup;
- удержание BACK на boot подавляет firmware autostart MicroPython;
- после watchdog reset показывается причина предыдущего аварийного завершения.

Startup script не должен запускаться из общего boot orchestration напрямую.
Сохраняется autostart ID приложения, но вход через boot или меню открывает один
и тот же безопасный список без запуска. Startup metadata задаёт сохранённый
default: файл выделяется первым и остаётся целью HMPY `RUN` без имени.

## IDE на базе Pybricks Code

Pybricks Code — MIT Web IDE на TypeScript/React с Monaco, project storage и
terminal UI. Это подходящая UI-база, но его transport и device model заточены
под LEGO Bluetooth/USB и Pybricks firmware. Рекомендуется отдельный fork с
зафиксированным upstream commit, сохранёнными `LICENSE`, copyright и страницей
attribution.

Оставить:

- Monaco editor, tabs/project persistence, file import/export;
- terminal component;
- installable PWA/offline shell;
- общую state-management структуру там, где она не зависит от LEGO hub.

Заменить/удалить:

- BLE hub transport, LEGO device discovery и firmware installer;
- Pybricks-specific mpy-cross/firmware metadata;
- LEGO API docs и анализатор, заменив их stubs/docs модуля `hackylens`;
- run/download flow, заменив одним HackyLens Web Serial client.

Web Serial доступен только в secure context и не поддерживается всеми
популярными браузерами. Целевые браузеры web-версии — Chromium/Edge desktop;
нужны HTTPS или localhost. Для Firefox/Safari и полностью офлайн Windows стоит
собирать desktop wrapper с тем же protocol client, не меняя firmware protocol.

MVP IDE должен уметь connect, HELLO, list/read/upload/delete, run/stop, live
stdout, выбор startup и recovery после disconnect. Форматирование, autocomplete
и симулятор — последующие функции.

## Лицензии

- HackyLens — MIT.
- MicroPython — MIT, copyright/permission notice сохраняется.
- Pybricks Code — MIT, copyright Pybricks Authors сохраняется, добавляется
  видимая attribution.
- littlefs — BSD-3-Clause, notice входит в source и binary distribution docs.
- MaixPy-v1 — смесь MIT/Apache-2.0; любой заимствованный файл проверяется
  отдельно и отмечается в `NOTICE`.

Перед release нужен автоматически генерируемый third-party manifest с версией,
commit SHA, license и перечнем включённых файлов.

## Порядок реализации и контрольные точки

### Этап 0 — hardware proof

1. Снять JEDEC ID и подтвердить 16 МБ минимум на нескольких SEN0305.
2. Протестировать erase/program/read `0x00C70000` без повреждения settings,
   firmware и возможности восстановления оригинального пакета.
3. Добавить flash-layout descriptor и bounds guard в build/release/flasher.

Gate: 100 циклов write/read и power-cut fault campaign без записи вне раздела.

### Этап 1 — storage и protocol

1. Усилить `boot_flash`, добавить littlefs block device и host fault tests.
2. Реализовать атомарные upload/list/read/delete/startup metadata.
3. Реализовать framed protocol и Python CLI reference client до IDE.

Gate: upload с обрывом на каждом chunk оставляет старый файл или отсутствие
файла, но никогда частично опубликованный файл; line debug восстанавливается.

### Этап 2 — минимальная VM

1. Интегрировать pinned upstream embed без bindings.
2. Запускать source на core 1, выводить stdout и состояние на core 0.
3. Добавить heap/stack/time limits, hooks, STOP, deinit и watchdog fallback.

Gate: normal return, syntax/runtime error, OOM, recursion, infinite loop,
sleep и stdout flood завершаются предсказуемо; 1000 run/stop циклов не дают
роста C heap и не ломают меню.

### Этап 3 — bindings v1 и on-device app

1. Buttons/time, затем LED/RGB, затем display, UART/I2C.
2. Для каждого binding добавить lease, timeout, cancel и cleanup tests.
3. Добавить browser/log/startup UI и safe boot.

Gate: STOP на каждом blocking API возвращает управление или предлагает
контролируемый restart; после остановки все ресурсы в исходном состоянии.

### Этап 4 — IDE

1. Зафиксировать fork Pybricks Code и оформить attribution.
2. Добавить transport client и MVP file/run/log UI.
3. Проверить reconnect, duplicate chunks, CRC errors и firmware mismatch.

Gate: полный workflow edit → atomic upload → run → live logs → stop → run from
device проходит без SD и без ручных debug-команд.

### Этап 5 — camera/KPU расширение

Только после измерений latency, RAM и cleanup v1. Сначала read-only vision
results, затем camera snapshots, затем модели. Каждый шаг сохраняет существующие
single-owner contracts.

## Фактический результат реализации и проверки

Исследовательские прототипы подтвердили, что для K210 не нужен MaixPy fork.
Официальный MicroPython v1.28.0 embed на commit
`e0e9fbb17ed6fd06bb76e266ae554784c9c80804` сгенерировал 135 C-файлов; все они
скомпилировались GCC 8.2 с теми же `rv64imafc/lp64f` flags, что основная прошивка,
и связались с HackyLens. Минимальная VM добавила 135038 байт text, 8 байт data и
776 байт BSS без GC heap. RV64 NLR (`py/nlrrv64.c`) и сохранение регистров GC
уже входят в upstream.

По результатам аудита первоначальный дизайн уточнён:

- stack core 1 физически ограничен linker script размером 32 КиБ, поэтому VM
  получает консервативный лимит 24 КиБ от локального stack anchor, а не все
  32 КиБ;
- GC heap сделан статическим, выровненным и принадлежащим только core 1,
  размером 128 КиБ;
- mailbox, runtime control/source/output и binding RPC размещены в uncached
  alias; одних `volatile` и fences для K210 недостаточно;
- flash и периферия остаются core-0-only. Python bindings выполняются через
  bounded single-flight RPC, а STOP является кооперативным исключением из VM
  hooks и cancel points;
- binding RPC удерживает single-slot context до final core-0 cancel ack, поэтому
  NLR unwind не может оставить позднюю hardware-операцию. UART TX продвигается
  неблокирующими tick-шагами с 8-байтной FIFO и завершается только после
  `TFL==0 && TEMT`; I2C имеет 100-мс deadline и cancel points;
- GC hook только обновляет heartbeat: поднимать исключение внутри сборщика
  нельзя, иначе GC может остаться в промежуточном состоянии;
- native iterator gateways опрашивают тот же stop/deadline hook, а просроченный
  кооперативный STOP имеет one-shot WDT1 fallback с safe-boot suppression;
- HackyLens Code выделен в отдельный repository `hackylens-code` с полным
  buildable source tree на проверенном Pybricks Code pin и материализованными
  HackyLens transport/UI/tests. Upstream MIT license, attribution и pin сохранены;
  большие LEGO firmware/video assets не включены.

Реализованы canonical flash map, guarded NOR driver, littlefs v2.11.2, явное
форматирование, atomic upload `temporary + sync + CRC32 + rename`, startup
metadata, cleanup незавершённых upload, core-1 MicroPython runtime, bindings v1,
on-device приложение, HMPY v1, Python CLI и отдельный HackyLens Code repository
с Monaco/Web Serial. IDE имеет собственный CI/build lifecycle, выпускается
отдельно и больше не входит в firmware/SD-card release archive.

Software-side результаты:

- fault harness проходит 53 возможных обрыва mutation sequence, а также CRC
  rejection, abort, delete/startup и stale-upload сценарии;
- 57 Python host-тестов проходят без skip, включая Python/C golden vectors, corruption,
  fragmentation, bounds/resync, 53 fault cuts, полный fake-device
  storage/runtime workflow, безопасный hardware-acceptance runner и executable
  harness реального binding/HAL/LCD state machine;
- 13 TypeScript codec/client tests в трёх suites используют те же golden vectors, полный
  fake-device workflow и long-operation/keepalive gate; strict TypeScript check
  и production webpack build проходят;
- `check_arch.py`, `gen_flash_layout.py --check` и `check_env.py` проходят для
  текущего 12-модульного дерева, 16-МиБ flash map и pinned host/target toolchains;
- full firmware: text/data/BSS `1346182/158152/2697472`, бинарник 1504440 байт,
  SHA-256 `5F265CB4DDF14E1B2C3E852725973782EBD91D39C415CB422943A48B469D7FBD`;
- symbol gate подтверждает MicroPython/runtime/HMPY/userfs и явно требует
  `mp_builtin_min_obj`, `mp_builtin_max_obj`, `mp_builtin_sum_obj`; production
  release-бинарник побайтно совпадает с проверенной full-сборкой;
- фактическая строка release/HELLO в ELF равна canonical `VERSION=0.2.0`;
- сборка с `--disable-app micropython` не содержит MicroPython, HMPY и userfs
  symbols, имеет text/data/BSS `1157879/158112/2418448`, бинарник 1316088 байт
  и SHA-256 `3ED2FA8E9F2D357F85EF4BFA5B491645824DEC6C9106493A064EF6C2E7BC11D9`;
- браузерная проверка production IDE при 1280x720 подтверждает Monaco, все
  disabled/initial states, локальные действия и отсутствие console errors.

Для прошивки физического SEN0305 выбран production-образ с тем же SHA-256;
flasher завершил запись без общего erase, поэтому settings, legacy area и userfs
не стирались. Boot log подтвердил JEDEC `EF6018` (16 МиБ), сохранённые settings,
12 приложений и работающую OV2640. HMPY `HELLO` возвращает firmware `0.2.0`, board
`HackyLens K210`, capabilities `511`; на живой камере прошёл stress из 100
последовательных `PING` с максимальным 1024-байтным payload. ISP stub не
предоставляет flash-read command, поэтому опция flasher `--verify` честно
завершилась предупреждением: версия и transport проверены на устройстве, но
криптографический readback именно выбранного бинарника отсутствует.

Первый mount обнаружил существующий повреждённый userfs:
`filesystem_state=3` (`CORRUPT`) и `filesystem_error=5` (`CORRUPT`). Firmware
правильно отказалась форматировать его автоматически. После отдельного явного
разрешения принят точный destructive token `ERASE USERFS`; раздел успешно
отформатирован и смонтирован. Полный аппаратный workflow на production-прошивке
прошёл: atomic upload/read/LIST/STAT, выбор и запуск startup, непрерывные логи,
cooperative STOP (123 мс), STOP внутри native `sum` (121 мс) и `min` (121 мс),
повторное использование executor, потеря transport без `SESSION_CLOSE`,
переподключение после lease и STOP нового run (122 мс). Cleanup удалил только
пять acceptance-файлов, снял startup и вернул userfs к пустому mounted-состоянию.
Локальная machine-readable трасса находится в gitignored каталоге
`build/hardware-acceptance`: `com10-format-full-workflow.json` содержит успешный
шаг `format` (общий запуск затем завершился ошибкой старой host-проверки STDERR),
а `com10-postformat-readonly.json` отдельно подтверждает mounted пустой раздел.
Финальные `com10-full-workflow-final.json` и `com10-final-clean-readonly.json`
имеют общий результат `PASS`.

Таким образом, основная CLI/HMPY часть аппаратного workflow прошла. Это не
закрывает независимые hardware/release gates первоначального плана: более
длительными или disruptive release-qualification проверками остаются
JEDEC/layout confirmation на нескольких SEN0305, erase/program/read endurance и
power-loss campaign физической NOR, живой browser Web Serial
edit/upload/run/log/stop/reconnect, WDT1 reset-cause/startup suppression,
физический BACK cleanup и 1000-cycle run/stop stress. До их прохождения результат
нельзя расширять до заявления о полной hardware-ready квалификации release.

WDT1 gate воспроизводится без опасного production binding: отдельная сборка
`--wdt-fault-injection` получает видимую версию `-wdtfi` и намеренно зависает в
VM hook после STOP/deadline. До trigger снимается read-only baseline, а после
физического reset отдельный read-only postcheck сверяет boot flag, idle state,
файлы, startup metadata и отсутствие autostart. Точная процедура описана в
[WDT_HARDWARE_ACCEPTANCE.md](WDT_HARDWARE_ACCEPTANCE.md).

## Основные риски

| Риск | Мера |
| --- | --- |
| Не все ревизии имеют 16-МБ flash | JEDEC allowlist, feature disabled на 8 МБ |
| VM блокирует интерфейс | core 1 + VM hooks + bounded bindings |
| Нельзя безопасно убить зависший C binding | timeout/cancel; затем только controlled reboot |
| Cache coherency между ядрами | uncached mailbox или явный cache flush/invalidate, barriers и hardware stress test |
| C/GC heap конфликтуют с AI и статическими буферами | app exclusivity, статический 128-КиБ GC heap, map/heap instrumentation |
| Износ/corruption flash | littlefs, atomic rename, CRC32 upload, fault injection |
| stdout ломает команды | exclusive framed session и отдельные event frames |
| IDE fork быстро расходится с upstream | тонкий transport layer, pinned commit, регулярный controlled rebase |
| Startup script создаёт boot loop | удержание BACK, countdown, one-boot suppression после watchdog |

## Источники

- [MicroPython embed port](https://github.com/micropython/micropython/tree/master/ports/embed)
- [Официальный embedding example](https://github.com/micropython/micropython/tree/master/examples/embedding)
- [MicroPython v1.28.0](https://github.com/micropython/micropython/releases/tag/v1.28.0)
- [MicroPython porting guide](https://docs.micropython.org/en/latest/develop/porting.html)
- [MicroPython C modules and heap model](https://docs.micropython.org/en/latest/develop/cmodules.html)
- [MicroPython VM hook configuration](https://github.com/micropython/micropython/blob/master/py/mpconfig.h)
- [MaixPy-v1 for K210](https://github.com/sipeed/MaixPy-v1)
- [littlefs design and guarantees](https://github.com/littlefs-project/littlefs)
- [Pybricks Code](https://github.com/pybricks/pybricks-code)
- [Web Serial API limitations](https://developer.mozilla.org/en-US/docs/Web/API/Web_Serial_API)
