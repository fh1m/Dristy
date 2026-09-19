---
contract-id: hackylens.architecture-vision
owner: platform-architecture
version: 0.1.0
stability: experimental
---

# Architecture Vision

## Статус документа

Этот документ определяет верхнеуровневую цель проекта и имеет приоритет над
локальными решениями отдельных приложений. Изменение этих принципов требует
явного архитектурного решения, а не побочного эффекта реализации новой функции.

Текущая HackyLens v0.4 является reference implementation и technology
preview. Она доказывает жизнеспособность части решений, но ещё не доказывает
аппаратную независимость или статус стандарта.

> HackyLens v0.4 is a layered K210 reference firmware and MicroPython technology
> preview.

## Цель

HackyLens создаётся не только как альтернативная прошивка для одного устройства.
Цель проекта — разработать лёгкую, переносимую и формально описанную application
platform для robotics hardware.

Платформа должна позволять:

- переносить прошивку на разные устройства с Kendryte K210 без форка core и apps;
- собирать прошивку из изолированных feature apps;
- включать и исключать приложения на этапе сборки;
- писать и тестировать приложение независимо от других приложений;
- быстро прототипировать поведение на MicroPython;
- переносить подтверждённый Python-прототип в native feature app без повторной
  разработки аппаратной интеграции;
- сохранять малый размер, предсказуемое потребление памяти и понятный lifecycle;
- предоставлять conformance tests для board ports, capabilities и приложений.

HackyLens SEN0305 является первой платой, на которой развивается и проверяется
архитектура. Следующие K210-устройства должны подключаться как отдельные Board
Support Packages.

## Позиционирование

Каноническая формулировка на текущем этапе:

> HackyLens develops a candidate open application architecture for programmable
> robotics hardware, with K210 devices as the first reference platform.

Формулировка «стандарт для K210 robotics hardware» допустима только после
выполнения gates, перечисленных в разделе «Путь к стандарту».

## Что является основным продуктом

Основным результатом проекта является не отдельная прошивка, приложение,
MicroPython runtime или IDE, а их общий архитектурный контракт:

1. спецификация слоёв и направления зависимостей;
2. Board Port contract;
3. Platform Capability API;
4. App Runtime и Feature App SDK;
5. единый project/manifest format;
6. MicroPython adapter к тем же capabilities;
7. build composition и dependency validation;
8. conformance test suite;
9. reference ports и reference applications;
10. developer tooling и IDE.

## Архитектурная модель

```text
User project
  |-- MicroPython prototype
  `-- Native feature app
              |
        Common App SDK
              |
        Application Runtime
              |
      Platform Capability API
              |
       Portable Services
              |
      Drivers / K210 Platform
              |
     Board Support Package
```

MicroPython и native apps являются двумя потребителями одной платформы. Они не
должны образовывать две несовместимые архитектуры.

## Слои

### 1. Board Support Package

BSP описывает только конкретную плату:

- pin mapping и FPIOA;
- clock tree и boot sequence;
- flash geometry и разделы;
- подключение LCD, camera, buttons, LEDs и внешних разъёмов;
- доступные периферийные блоки;
- board-specific reset, ISP и power behavior;
- capability inventory платы.

Ни одно feature app не знает номер пина, экземпляр SPI/UART/I2C или JEDEC ID.
Прошивка выбирает BSP явным `--board`, а не изменением исходников.

### 2. K210 Platform/HAL

HAL предоставляет узкие операции K210:

- GPIO, FPIOA, clocks и interrupts;
- SPI, I2C и UART;
- DVP и KPU;
- DMA и core control;
- flash и watchdog;
- monotonic time.

HAL не знает о меню, feature apps, MicroPython, project files или пользовательских
сценариях.

### 3. Drivers

Drivers реализуют конкретные устройства и wire protocols: OV2640, ST7789, SD,
NOR flash, buttons, LEDs и другие компоненты. Driver знает устройство, но не
знает, какое приложение его использует.

### 4. Platform Services

Services предоставляют переносимые возможности:

- camera и frame ownership;
- display composition;
- vision algorithms;
- AI model runtime;
- input events;
- storage и settings;
- external link;
- power management;
- logging и diagnostics;
- resource ownership.

Каждый service contract определяет owner, lease, timeout, cancellation, cleanup,
memory budget, concurrency и error model. Shared service не зависит от feature
app или MicroPython binding.

### 5. Core/Application Runtime

Application Runtime является лёгким OS-подобным ядром платформы:

- registry и manifest resolution;
- app lifecycle;
- event dispatch;
- ticks, deadlines и background work;
- capability discovery и injection;
- resource context;
- switching, autostart и safe mode;
- crash/cleanup policy;
- power-state coordination.

«OS-подобный» не означает обязательную тяжёлую ОС. Приоритет имеют статическая
композиция, ограниченная память, отсутствие скрытых аллокаций и предсказуемое
время исполнения.

### 6. Feature Apps

Пользовательская функция реализуется как изолированный `apps/<feature>/`:

- один публичный app header;
- manifest;
- controller/model/view/config внутри модуля;
- зависимости только от App SDK и объявленных capabilities;
- отдельные host tests;
- собственное состояние без публичных mutable globals;
- единый lifecycle и гарантированный cleanup;
- полное исключение из сборки без остаточных private sources.

Приложение не включает HAL, board headers или private headers другого приложения.

### 7. MicroPython Prototyping Layer

MicroPython является адаптером к Platform Capability API. Binding не должен
реализовывать отдельный аппаратный путь, если capability уже используется native
apps.

Например, Python и C используют одну семантику camera frame:

```python
with app.camera.capture() as frame:
    objects = app.vision.find_blobs(frame, thresholds)
    app.display.show(frame)
```

```c
hk_frame_t frame;
hk_camera_capture(ctx->camera, &frame);
hk_vision_find_blobs(ctx->vision, &frame, &thresholds, &objects);
hk_display_show(ctx->display, &frame);
hk_frame_release(&frame);
```

Имена языковых API могут учитывать особенности языка, но lifetime, координаты,
цветовые форматы, ошибки, deadlines и cleanup должны совпадать.

### 8. Tooling и IDE

IDE является клиентом project format, device protocol и platform metadata. Она
не определяет архитектуру firmware.

IDE должна работать с проектами, manifests, локальным состоянием и синхронизацией
с устройством. Transport, editor state, device state и UI layout должны быть
отдельными подсистемами с fake-device E2E tests.

## Главный архитектурный закон

> Новая общая возможность не добавляется непосредственно в feature app,
> MicroPython binding или BSP. Сначала определяется переносимый capability
> contract, затем его platform implementation, после чего добавляются адаптеры
> для native apps и MicroPython.

Следствия:

- apps зависят от capabilities;
- capabilities никогда не зависят от apps;
- Python не получает привилегированный обходной путь к hardware;
- BSP не содержит product logic;
- driver не содержит app policy;
- shared service не знает конкретного UI flow;
- отсутствие capability определяется явно, а не приводит к link/runtime surprise.

## Быстрота использования

Архитектурная строгость не должна превращать создание приложения в тяжёлый
процесс. Стандартный путь должен быть коротким:

```powershell
python tools/new_project.py color-tracker --runtime micropython
python tools/run_project.py color-tracker --board huskylens-sen0305
python tools/new_app.py color-tracker --from-project projects/color-tracker
python tools/build_firmware.py full --board huskylens-sen0305 --enable-app color-tracker
```

Boilerplate, manifest validation, registry generation, fakes и test skeleton
создаются инструментами. Ручной код остаётся в domain logic приложения.

## Переход Python -> native app

Полная автоматическая трансляция произвольного Python в C не является целью.
Цель — сделать перенос прямолинейным:

1. Python prototype использует только public capabilities;
2. state machine отделена от UI и hardware;
3. manifest фиксирует capabilities и limits;
4. `new_app.py --from-project` создаёт native skeleton;
5. Python calls заменяются зеркальными C calls;
6. одинаковые fixtures проверяют обе реализации;
7. native app получает performance и startup integration без изменения services.

## Compile-time composition

Платформа остаётся compile-time composable:

- board выбирается явно;
- apps включаются и исключаются manifests/build profile;
- неиспользуемые drivers/services/third-party libraries удаляются;
- dependency graph валидируется до компиляции;
- сборка публикует flash/RAM/capability report;
- несовместимая конфигурация завершается понятной ошибкой.

Dynamic ELF plugins, MMU и тяжёлый package manager не входят в baseline. Их нельзя
добавлять без доказанной необходимости и измеренного бюджета.

## Путь к стандарту

Архитектура может называться стандартом только когда существуют:

1. versioned normative specification;
2. стабильные Board, Capability, App и Project contracts;
3. conformance runner;
4. минимум две существенно разные K210-платы;
5. одинаковые unmodified feature apps на обеих платах;
6. независимое приложение, созданное без изменения platform core;
7. минимум два доказанных Python-to-native переноса;
8. правила backward compatibility и deprecation;
9. измеримые memory, timing и cleanup guarantees;
10. release qualification на физическом hardware.

До закрытия этих gates проект является candidate architecture с одной reference
implementation.

## Правила принятия решений

Каждая крупная работа должна отвечать на вопросы:

- к какому слою относится изменение;
- является ли оно board-specific или portable;
- какой public contract добавляется или меняется;
- кто владеет ресурсом и кто выполняет cleanup;
- как feature отключается из сборки;
- как capability тестируется без hardware;
- как поведение проверяется на устройстве;
- доступно ли оно одинаково native app и MicroPython;
- влияет ли оно на переносимость существующих apps;
- требуется ли Architecture Decision Record.

Если ответы отсутствуют, работа не готова к реализации.
