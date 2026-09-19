# HackyLens Platform Roadmap

## Назначение

Этот roadmap переводит HackyLens из firmware-specific проекта в candidate open
application architecture для robotics hardware.

Верхнеуровневая цель и архитектурные законы зафиксированы в
[ARCHITECTURE_VISION.md](ARCHITECTURE_VISION.md). Честная исходная точка описана
в [CURRENT_STATE.md](CURRENT_STATE.md).

Roadmap не является списком независимых функций. Этапы имеют зависимости и
exit gates. Работа следующего слоя не считается завершённой, пока не доказан
контракт предыдущего.

## Текущий статус

HackyLens v0.4 классифицируется как:

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

Она уже доказывает:

- возможность feature-based firmware composition;
- работу camera/KPU/UI/storage subsystems;
- встроенный MicroPython на core 1;
- USERFS/HMPY/Web Serial vertical slice;
- bounded STOP, cleanup и аппаратное восстановление;
- возможность host и hardware acceptance testing.

Она ещё не доказывает:

- переносимость на вторую K210-плату;
- стабильный App SDK;
- manifest-driven composition;
- OpenMV-class Python vision workflow;
- production-grade IDE;
- parity с оригинальной прошивкой;
- статус архитектурного стандарта.

## Приоритеты

Приоритеты имеют фиксированный порядок:

1. **Архитектурная целостность и переносимость.**
2. **Простота создания изолированного приложения.**
3. **Быстрый Python prototyping и прямой путь к native app.**
4. **Пользовательский workflow на устройстве и в IDE.**
5. **Расширение capabilities и parity.**
6. **Оптимизация и release qualification.**

Новая функция не может обойти первые три приоритета ради короткого demo.

## Неподвижные правила реализации

### Platform-first

Общая возможность сначала получает capability contract. Запрещено сначала
реализовывать app-specific или Python-only путь, а затем объявлять его platform
API.

### Board-independent apps

Feature app не включает board/HAL headers и не знает номера peripherals/pins.
Hardware различия выражаются BSP и capability inventory.

### One source of composition truth

App manifest должен генерировать registry, build definitions, capability checks
и metadata. Ручное дублирование списков считается временным legacy.

### Symmetric native/Python semantics

Native и MicroPython adapters используют одинаковый service implementation,
lifetime, coordinates, formats, errors, deadlines и cleanup.

### Measured lightweight design

Каждый новый runtime abstraction публикует flash, static RAM, stack и latency
impact. Не допускаются скрытые heap allocations или unbounded queues.

### Evidence before status

Статус `done` требует source, tests, build evidence и соответствующий hardware
gate. Наличие UI или API signature без end-to-end работы не закрывает пункт.

## Карта зависимостей

```text
Phase 0: Governance/spec baseline
   |
Phase 1: Board Port Contract -----------+
   |                                    |
Phase 2: Capability Platform            |
   |                                    |
Phase 3: App Runtime + Feature SDK      |
   |              |                     |
Phase 4: Program Manager                |
   |              |                     |
Phase 5: MicroPython API v2              |
   |              |                     |
Phase 6: Python -> Native workflow       |
   |                                    |
Phase 7: Second K210 reference board <---+
   |
Phase 8: IDE Beta
   |
Phase 9: Original firmware parity
   |
Phase 10: Conformance and 1.0 qualification
```

IDE design может исследоваться параллельно, но project/device model должен
строиться на contracts Phases 1-4. Новые parity apps реализуются только через
Capability/App SDK.

---

## Phase 0 — Architecture governance

### Цель

Сделать архитектуру самостоятельным versioned продуктом и прекратить изменение
публичных границ неявными implementation decisions.

### Работы

- [x] Зафиксировать architecture vision.
- [x] Зафиксировать current-state audit.
- [x] Определить platform-first roadmap.
- [x] Ввести `docs/spec/` для нормативных контрактов.
- [x] Ввести Architecture Decision Records в `docs/adr/`.
- [x] Определить термины `board`, `platform`, `driver`, `service`, `capability`,
  `runtime`, `app`, `project` и `adapter`.
- [x] Определить semantic versioning отдельно для firmware, Platform API,
  App SDK, Project Format и HMPY.
- [x] Определить experimental/stable/deprecated lifecycle публичного API.
- [x] Добавить documentation consistency checks для ссылок, version fields и
  запрещённых overclaims.
- [x] Пометить v0.2 как technology preview во всех entry documents.

### Артефакты

- `docs/ARCHITECTURE_VISION.md`;
- `docs/CURRENT_STATE.md`;
- `docs/spec/GLOSSARY.md`;
- `docs/spec/VERSIONING.md`;
- `docs/adr/0001-*.md`;
- template для новых ADR.

### Exit gate

- [x] Любой новый public contract имеет owner, version и stability label.
- [x] Pull request template требует указать затронутые layers/capabilities.
- [x] README не называет v0.2 OpenMV-class или hardware-independent platform.

### Evidence

- normative index, glossary и versioning policy находятся в `docs/spec/`;
- ADR process, template и accepted ADR-0001 находятся в `docs/adr/`;
- `tools/check_docs.py` проверяет metadata, локальные links/anchors, canonical
  version sources, ADR structure, entry markers и точные forbidden claims;
- `tests/test_docs_contracts.py` содержит positive repository gate и negative
  fixtures, а GitHub Actions запускает documentation guard для каждого change;
- `.github/pull_request_template.md` требует architecture, contract,
  compatibility и reliability evidence;
- hardware smoke: `N/A`, поскольку Phase 0 не изменяет firmware source,
  generated firmware inputs, device behavior или capabilities.

---

## Phase 1 — Board Port Contract

### Цель

Отделить K210 platform и product firmware от SEN0305. Новая K210-плата должна
добавляться новым BSP, а не форком repository.

Статус: **DONE**. Software implementation, automated evidence и физический
SEN0305 smoke завершены; evidence зафиксирован в
`docs/evidence/phase1-hardware-smoke.json`.

### 1.1 Board descriptor

Определить versioned descriptor:

```toml
schema = 1
id = "huskylens-sen0305"
platform = "kendryte-k210"
support = "runtime"
releaseable = true
runtime_profile = "hackylens-full"
```

Descriptor описывает hardware inventory, routes, defaults и programming
metadata, а не app policy. Private platform registry фиксирует допустимые device
kinds, drivers, FPIOA functions, peripherals и runtime profiles.

### 1.2 Source layout

Целевой layout:

```text
platforms/k210/
  hal/
  startup/
  devices.toml
boards/huskylens-sen0305/
  board.toml
  board.c
  flash_layout.json
  generated/
    pins.h
    defaults.h
    inventory.h
    flash_layout.h
boards/sipeed-maix-cube/
  ...
firmware/src/internal/
  time_internal.h
  boot_internal.h
  memory_internal.h
```

Старые `firmware/src/board`, `firmware/src/hal` и forwarding headers удалены.

### 1.3 Build selection

- [x] Добавить обязательный `--board` без implicit/default board.
- [x] Генерировать board configuration и driver-supported inventory.
- [x] Валидировать canonical flash layout относительно descriptor.
- [x] Делать board-specific artifact names.
- [x] Публиковать canonical `board.toml.id` в firmware metadata и HMPY HELLO.
- [x] Проверять image/board/profile/composition через hash-bound build
  attestation, sidecar, flasher и package metadata.

### 1.4 Remove leaked assumptions

Провести inventory всех:

- pin/peripheral constants;
- display dimensions/rotation;
- camera wiring и sensor assumptions;
- button masks;
- flash capacity/partition assumptions;
- SD chip-select и SPI clock;
- external connector mapping;
- reset/ISP behavior.

Каждый факт должен перейти в BSP, driver descriptor или явно generic platform
constant.

### 1.5 Board conformance harness

Проверять:

- descriptor schema;
- required callbacks;
- unique pins/peripherals;
- partition alignment/overlap;
- device-to-driver mapping;
- generated config;
- smoke build;
- board metadata and build-attestation identity.

### Exit gate

- Full firmware собирается только с явным board target.
- `huskylens-sen0305` не используется как implicit global assumption.
- `sipeed-maix-cube` проходит compile-time descriptor/BSP conformance, но не
  считается runtime/release/hardware-qualified портом.
- Ни один feature app не включает BSP/HAL headers.

### Evidence

- Host contract/docs/architecture checks проходят без skip; CI проверяет
  явный multi-commit diff range через `git diff --check`.
- `huskylens-sen0305` проходит feature-disabled и full cross-build;
  `sipeed-maix-cube` проходит descriptor/BSP compile-link conformance и явно
  отклоняет full/release/flash.
- Phase 1 resource result: erase-rounded flash delta `-4096`, static RAM delta `-176`,
  новых heap allocations/background tasks/queues не обнаружено.
- Физический SEN0305 smoke (boot/display/camera/buttons/lights/SD/HMPY/external
  link runtime routing/transmit/package-flash round trip) пройден 2026-08-13;
  отсутствие ISP readback и external electrical loopback fixture явно отмечено
  в evidence и не выдаётся за qualification.
- Cube hardware qualification отложена; hardware-independence не заявляется.

---

## Phase 2 — Platform Capability API

Подробная последовательность отдельных implementation-пакетов и их локальные
exit gates зафиксированы в [PHASE2_MASTERPLAN.md](PHASE2_MASTERPLAN.md).

Статус: **DONE**. Пять начальных capabilities реализованы и квалифицированы на
SEN0305; automated result, owner hardware acceptance и closure chain находятся
в `docs/evidence/phase2-candidate-result.json`,
`docs/evidence/phase2-hardware-smoke.json` и
`docs/evidence/phase2-closure-result.json`. Maix Cube остаётся
compile-conformance-only; общая hardware portability не заявляется.

### Цель

Создать единственную переносимую поверхность hardware возможностей для native
apps и language adapters.

### 2.1 Common capability model

Определить typed handles только для пяти начальных capabilities:

```c
typedef struct hk_time_capability hk_time_capability_t;
typedef struct hk_input_capability hk_input_capability_t;
typedef struct hk_display_capability hk_display_capability_t;
typedef struct hk_external_link_capability hk_external_link_capability_t;
typedef struct hk_lights_capability hk_lights_capability_t;
```

Phase 2 выдаёт handles через private runtime wiring и generated immutable
inventory. Public `hk_app_context`, public app manifest/project format и их
capability declarations вводятся не раньше Phase 3.

Каждый capability contract содержит:

- version и feature flags;
- acquire/release policy;
- owner identity;
- bounded operations;
- cancel token;
- error enum;
- cleanup guarantee;
- thread/core affinity;
- memory ownership;
- test fake;
- hardware acceptance cases.

### 2.2 Error/time model

Создать общие типы:

- `hk_result_t`;
- `hk_deadline_t`;
- `hk_cancel_t`;
- `hk_owner_t`;
- `hk_lease_t`;
- `hk_buffer_view_t`.

Запретить бесконечное ожидание hardware. Deadline относится к операции целиком,
а не обновляется на каждой строке/чанке.

### 2.3 Initial capabilities

Мигрировать в таком порядке:

1. time;
2. input/buttons;
3. lights;
4. display;
5. external UART/I2C;

Public storage, camera/frame, vision и AI model runtime capabilities, а также
power/settings/logging expansion, остаются Phase 3+. Phase 2 может переносить
существующие private storage/frame-pool operations между внутренними слоями
только для закрытия architecture guard; это не создаёт public capability.

### 2.4 Capability discovery

Board/build создаёт immutable inventory. Private Phase 2 consumer wiring может:

- потребовать capability и не включиться без неё;
- объявить optional capability и получить явный fallback;
- запросить supported operations/version;
- не угадывать hardware по board ID.

Public app-facing discovery и manifest integration остаются Phase 3+.

### 2.5 Architecture guard v2

Добавить запреты:

- apps -> board/hal/drivers;
- MicroPython adapter -> board/hal/drivers;
- capability -> apps;
- service -> feature private headers;
- board -> apps/product policy;
- duplicate Python-only hardware implementation.

### Exit gate

- Buttons, display и external-link работают через capability handles.
- Existing native app и MicroPython adapter используют один implementation.
- Capability fake выполняет одинаковые contract tests.
- Отключение capability приводит к понятной build error или optional fallback.

---

## Phase 3 — App Runtime v2 и Feature App SDK

### Цель

Сделать приложение самостоятельной переносимой единицей, которую можно создать,
собрать, тестировать, отключить и перенести без изменения platform core.

### 3.1 Lifecycle v2

Целевой lifecycle:

```c
probe(ctx)
prepare(ctx)
start(ctx)
event(ctx, event)
tick(ctx, now)
render(ctx, surface)
stop(ctx, reason)
cleanup(ctx)
```

Требования:

- `prepare` не оставляет resources при ошибке;
- `start` вызывается только после успешного capability injection;
- `stop` идемпотентен;
- `cleanup` вызывается для normal, BACK, exception, timeout и forced switch;
- background work не использует уничтоженное app state;
- screen/menu являются adapters runtime, а не идентичностью приложения.

### 3.2 Manifest schema

Пример:

```toml
schema = 1
id = "color-tracker"
name = "Color Tracker"
version = "0.1.0"
entry = "color_tracker_app"

[requires]
camera = ">=1,<2"
display = ">=1,<2"
input = ">=1,<2"
vision_blobs = ">=1,<2"

[limits]
static_ram = 65536
tick_us = 10000
stack = 8192
```

### 3.3 Generated composition

Manifest становится source of truth для:

- registry entry;
- build source inclusion;
- capability validation;
- autostart eligibility;
- menu metadata;
- help/debug metadata;
- memory report;
- IDE metadata;
- test matrix.

Ручной `app_registry.c` заменяется generated registry с небольшим stable runtime.

### 3.4 App SDK

Добавить:

- public headers только в `sdk/include`;
- CMake/make integration;
- fake platform;
- event/input types;
- view/surface contract;
- settings namespace;
- storage namespace;
- logging API;
- example apps;
- compatibility policy.

### 3.5 Generator

```powershell
python tools/new_app.py color-tracker
```

Создаёт:

- directory и manifest;
- public entry;
- controller/model/view split;
- lifecycle implementation;
- capability declarations;
- fake/test skeleton;
- architecture-valid build entry.

### 3.6 Migration proof

Сначала мигрировать простые BUTTONS/PONG, затем один camera app. Поведение до и
после сравнивается golden tests и hardware screenshots/results.

### Exit gate

- Новое sample app создаётся одной командой.
- Для его добавления не изменяются core, registry или build script вручную.
- App собирается отдельно с fakes и внутри full firmware.
- App целиком исчезает из image через build profile.
- Один camera app работает через injected capabilities.

---

## Phase 4 — Project Format и on-device Program Manager

### Цель

Сделать Python-программы полноценными проектами и позволить использовать их без
подключённого компьютера.

### 4.1 Project format

Определить project manifest:

```toml
schema = 1
id = "line-follower"
name = "Line Follower"
runtime = "micropython"
entry = "main.py"

[requires]
camera = ">=1,<2"
display = ">=1,<2"
vision_lines = ">=1,<2"

[runtime]
deadline_ms = 0
heap_bytes = 131072
```

Решить и документировать:

- multi-file packaging;
- USERFS naming/namespace;
- SD assets/models;
- atomic project update;
- project/version identity;
- startup selection;
- compatibility before run;
- rollback after interrupted upload.

### 4.2 PROGRAMS app

Отдельное пользовательское приложение:

- project/file list;
- выбранный entry point;
- run/stop;
- logs/errors;
- startup toggle;
- rename/delete;
- metadata/capability warnings;
- safe mode after WDT;
- return to list after completion;
- no-computer workflow.

Диагностический MicroPython экран не должен выполнять роль Program Manager.

### 4.3 Runtime integration

- activity/wake coordination;
- запрет auto-sleep во время foreground project без явной policy;
- cleanup before next project;
- retained logs;
- startup failure counter;
- safe boot через BACK;
- project-specific settings namespace.

### Exit gate

На отключённом от ПК устройстве пользователь может выбрать, запустить, остановить
и сменить минимум три проекта. Не требуется заранее назначать startup через IDE.

---

## Phase 5 — MicroPython Capability API v2

### Цель

Превратить MicroPython из demo API в среду быстрого robotics/computer-vision
prototyping поверх тех же capabilities, что используют native apps.

### 5.1 API structure

Перейти от одного плоского модуля к versioned namespaces без немедленного удаления
совместимости v1:

```python
from hackylens import app, camera, display, input, vision, kpu, storage
```

### 5.2 Frame model

Большие frames не копируются в GC heap. Использовать zero-copy borrowed handles:

```python
with camera.capture() as frame:
    roi = frame.roi(40, 30, 160, 120)
    blobs = vision.find_blobs(roi, thresholds)
    frame.draw_rect(blobs[0].rect)
    display.show(frame)
```

Контракт определяет:

- lifetime и invalidation;
- read/write access;
- frame sequence;
- pixel format;
- rotation/coordinates;
- capture timeout;
- conflict с KPU/display;
- release при exception/STOP.

### 5.3 Camera API

- sensor open/configure;
- capture;
- resolution/pixel format;
- exposure/gain/white balance;
- mirror/flip;
- ROI/crop/resize;
- statistics;
- frame drawing;
- display preview.

### 5.4 Vision API

Порядок реализации определяется reusable native capabilities:

1. grayscale/color conversion;
2. histogram/statistics/threshold;
3. blobs;
4. lines/segments;
5. rectangles/circles;
6. QR;
7. AprilTag;
8. face detection;
9. object detection;
10. tracking/recognition primitives.

Не требуется копировать OpenMV API буквально. Нужен небольшой стабильный API,
соответствующий памяти и ускорителям K210.

### 5.5 KPU API

Высокий уровень:

```python
objects = vision.detect_objects(frame, model="voc20")
```

Низкий уровень:

```python
model = kpu.load("/sd/models/custom.kmodel")
output = model.run(frame)
```

Определить model descriptors, tensor views, normalization, post-processing,
single-KPU ownership и cancellation.

### 5.6 Storage API

- project-local files;
- SD read/write streams;
- bounded buffers;
- image save/load;
- atomic settings/data;
- path sandbox;
- explicit distinction USERFS и SD.

### 5.7 Host compatibility package

Создать Python package с теми же types/API для:

- type hints;
- IDE completion;
- documentation;
- host unit tests;
- deterministic fakes;
- partial simulation.

### Exit gate

Три reference projects работают на физическом устройстве:

1. camera preview с overlay;
2. color/blob tracker;
3. KPU object detector.

Все используют public capabilities и имеют host tests без hardware.

---

## Phase 6 — Python prototype -> Native feature app

### Цель

Доказать главный developer workflow платформы, а не только его возможность на
бумаге.

### 6.1 Portable project structure

Python examples разделяют:

- domain state;
- events;
- capability calls;
- rendering;
- configuration.

Не поощряется неструктурированный бесконечный script, если проект планируется
переносить в native app.

### 6.2 Lifecycle mapping

Документировать точное соответствие:

| Project/Python | Native App |
| --- | --- |
| `on_start(ctx)` | `start(ctx)` |
| `on_event(ctx, e)` | `event(ctx, e)` |
| `on_tick(ctx)` | `tick(ctx, now)` |
| `on_render(ctx)` | `render(ctx, surface)` |
| `on_stop(ctx)` | `stop(ctx, reason)` |

### 6.3 Generator

```powershell
python tools/new_app.py color-tracker --from-project projects/color-tracker
```

Генерирует manifest, capability declarations, state/controller skeleton, view,
test fakes и migration checklist. Он не обещает автоматический перевод arbitrary
Python syntax.

### 6.4 Shared behavior tests

Одинаковые event/frame fixtures выполняются против:

- Python project logic;
- native C logic harness;
- при необходимости device acceptance.

### 6.5 Proof applications

Перенести минимум:

1. non-camera интерактивный проект;
2. camera/vision проект;
3. KPU проект до заявления platform standard.

Изменение platform services ради port допускается только если обнаружен общий
недостающий capability contract.

### Exit gate

Два Python проекта превращены в independently buildable feature apps. Отчёт
фиксирует изменённые строки domain logic, performance, flash/RAM и отсутствие
board-specific кода.

---

## Phase 7 — Second K210 reference board

### Цель

Доказать, что архитектура переносима, а не только хорошо организована внутри
одного устройства.

### Выбор платы

Выбрать K210 board, которая отличается хотя бы двумя существенными аспектами:

- display или его отсутствие;
- camera wiring/sensor;
- buttons/input;
- flash capacity/layout;
- SD/external connectors;
- reset/boot behavior.

### Работы

- [ ] Добавить BSP без изменения existing apps.
- [ ] Реализовать capability inventory.
- [ ] Собрать минимальный profile.
- [ ] Запустить console/buttons/display или headless equivalents.
- [ ] Запустить MicroPython runtime.
- [ ] Запустить один одинаковый portable Python project.
- [ ] Запустить один одинаковый native feature app.
- [ ] Зафиксировать hardware evidence и porting effort.
- [ ] Удалить обнаруженные SEN0305 assumptions из shared layers.

### Exit gate

Один unmodified project и один unmodified native app работают на двух boards.
Различия находятся только в BSP, board config и допустимых optional capability
fallbacks.

После этого hardware independence становится доказанным свойством версии, но ещё
не полным стандартом.

---

## Phase 8 — HackyLens Code IDE Beta

### Цель

Создать developer environment для Project Format и Platform API, сопоставимый по
целостности workflow с Pybricks Code, без копирования LEGO-specific architecture.

### 8.1 IDE architecture

Разделить:

- local workspace/project store;
- editor model;
- device filesystem model;
- sync engine;
- HMPY transport/session;
- runtime/log model;
- docs/completion metadata;
- visual layout/components.

### 8.2 Project workflow

- create/open/import/export project;
- multi-file tree;
- folders и rename;
- persistent drafts;
- manifest editor;
- local/device diff;
- atomic sync;
- selected entry/startup;
- examples/templates;
- recent projects;
- recovery после reconnect/reload.

### 8.3 Editor intelligence

- HackyLens type stubs;
- completion;
- signatures/docs;
- diagnostics;
- traceback -> source line;
- formatting/lint hooks;
- capability compatibility warnings;
- board/project selector.

### 8.4 Device experience

- explicit connect/disconnect state machine;
- board/firmware/API compatibility;
- Programs list;
- run/stop/status;
- structured logs;
- storage usage;
- safe format/recovery;
- reconnect without lost editor state;
- firmware update handoff.

### 8.5 Visual system

- design tokens;
- reusable components;
- resizable panes;
- responsive breakpoints;
- compact and comfortable density modes;
- keyboard navigation;
- accessibility;
- screenshot regression;
- no ad-hoc per-screen layout patches.

### 8.6 Tests

- fake Web Serial device;
- protocol faults/reconnect;
- complete project E2E;
- screenshot tests at supported viewports;
- persistence/reload;
- corrupted/incompatible project states;
- live hardware acceptance.

### Exit gate

Автоматизированный browser E2E выполняет:

```text
create multi-file project
-> edit manifest/source
-> connect
-> sync
-> run
-> receive log/error with source location
-> fix and rerun
-> set startup
-> reconnect/reload
-> preserve project and device state
```

Отдельный physical Web Serial gate повторяет основной flow на двух boards.

---

## Phase 9 — Original firmware parity as portable apps

### Цель

Восстановить полезные функции оригинальной HUSKYLENS, не встраивая legacy policy
в shared platform.

### 9.1 Formal inventory

Создать `ORIGINAL_FIRMWARE_PARITY.md` со столбцами:

- feature/mode;
- original behavior;
- recovered evidence;
- HackyLens implementation;
- status `unknown/missing/partial/equivalent/improved`;
- persistence format;
- external protocol behavior;
- hardware tests;
- target capability/app.

### 9.2 Likely missing areas

- face recognition и enrollment;
- object tracking;
- line tracking;
- color recognition;
- learned object/classification workflows;
- tag learning/selection compatibility;
- learned data persistence;
- complete sensor/settings behavior;
- exact UART/I2C compatibility mode;
- system/update/recovery flows.

Фактический список определяется evidence audit, а не памятью или UI labels.

### 9.3 Implementation rule

Каждая recovered функция разделяется на:

- reusable capability/algorithm service;
- isolated feature app;
- optional Python adapter;
- original-protocol compatibility adapter при необходимости.

Нельзя добавлять original firmware behavior напрямую в HAL или generic runtime.

### Exit gate

Parity matrix не содержит `unknown`. Все `missing/partial` либо закрыты, либо
явно объявлены out of scope с техническим обоснованием. Claims README ссылаются
на matrix/evidence.

---

## Phase 10 — Conformance, ecosystem и 1.0

### Цель

Превратить внутреннюю архитектуру в проверяемую внешнюю спецификацию и подготовить
стабильную платформу.

### 10.1 Normative specifications

Версионировать:

- Board Port Spec;
- Capability API Spec;
- App SDK Spec;
- Project Format Spec;
- HMPY/Device Protocol;
- resource/lifecycle/error model;
- package metadata;
- compatibility policy.

### 10.2 Conformance runner

Отдельные suites:

- board static validation;
- HAL/driver behavior;
- capability contracts;
- App SDK lifecycle;
- project/package validation;
- protocol codec;
- cleanup/fault injection;
- timing/memory budgets;
- physical device acceptance.

### 10.3 Independent extension proof

Новое приложение и по возможности board port создаются по публичной документации
без изменения platform core. Все недостающие инструкции фиксируются как spec/SDK
defects.

### 10.4 Qualification

- multi-device/multi-board runs;
- flash endurance/power loss;
- WDT fault injection;
- 1000-cycle app/Python start-stop;
- 1000-cycle reconnect/sync;
- camera/KPU long runs;
- stack/heap/static RAM watermarks;
- protocol fuzzing;
- firmware update с сохранением projects/data;
- rollback/recovery;
- compatibility upgrade from previous stable versions.

### 10.5 Standard claim gate

Проект может называться open application standard для K210 robotics hardware,
когда:

- normative specs опубликованы;
- conformance runner доступен;
- две разные K210 boards прошли tests;
- одинаковые apps/projects работают на обеих;
- два Python-to-native переноса опубликованы;
- independent app создан без core changes;
- API compatibility policy применяется хотя бы между двумя releases.

### v1.0 exit gate

- Architecture/API marked stable.
- No unresolved P0/P1 conformance failures.
- Programs работают без ПК.
- Camera/vision/KPU доступны Python и native apps.
- IDE проходит automated и physical E2E.
- Original parity scope закрыт или честно ограничен.
- Release artifacts воспроизводимы из clean clone.

---

## Постоянный reliability track

Reliability не откладывается до Phase 10. Для каждого phase обязательны:

- host contract tests;
- feature-disabled build;
- full build;
- architecture guard;
- memory/size delta;
- hardware smoke для затронутого capability;
- cancellation/cleanup cases;
- documentation update.

Destructive hardware tests всегда требуют явного opt-in и machine-readable
report. Ни один local ignored report не является долговременным release evidence;
значимые результаты должны сохраняться в tracked summary без device secrets.

## Definition of Done для capability

Capability считается готовым, если:

- есть versioned public contract;
- указаны required board resources;
- есть native adapter;
- есть MicroPython adapter либо documented reason его отсутствия;
- реализованы owner/lease/deadline/cancel/cleanup;
- нет app/board dependency inversion;
- есть fake и contract tests;
- есть full и absent build evidence;
- есть physical acceptance;
- измерены RAM/flash/latency;
- документация содержит examples и failure behavior.

## Definition of Done для feature app

App считается готовым, если:

- manifest является единственным composition source;
- app использует только App SDK/capabilities;
- app не включает board/HAL/private feature headers;
- state принадлежит app instance/context;
- lifecycle и forced cleanup протестированы;
- app отдельно тестируется с fakes;
- app полностью исключается из build;
- memory/tick budgets соблюдены;
- app запускается на каждой совместимой reference board;
- public behavior документирован.

## Definition of Done для board port

Board port считается готовым, если:

- descriptor проходит schema validation;
- pins/peripherals/flash не протекают в shared code;
- capability inventory точен;
- minimal/full compatible profiles собираются;
- flasher проверяет board metadata;
- board conformance и hardware smoke проходят;
- portable sample project и app работают без source changes;
- porting guide обновлён фактическими шагами.

## Ближайший исполнимый increment

Следующий increment не должен начинаться с camera binding или редизайна IDE.

Порядок:

1. создать normative glossary/versioning/ADR process;
2. спроектировать Board Port Spec и добавить `--board`;
3. спроектировать common result/deadline/owner/capability types;
4. мигрировать time/buttons как первый vertical capability;
5. ввести manifest schema и generated sample app;
6. только после этого начинать PROGRAMS и camera API.

Этот increment должен закончиться не количеством функций, а доказательством:

> Новая изолированная app создаётся генератором, получает только объявленные
> capabilities, собирается для выбранной board и не требует ручного изменения
> core/registry/build tables.
