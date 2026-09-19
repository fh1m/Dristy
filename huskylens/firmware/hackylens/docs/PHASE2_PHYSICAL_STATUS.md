# Phase 2 physical status

Статус на 2026-08-24: `completed` для DFRobot SEN0305 и точного candidate
image `d6fe174892d05a1403a9418dcd8acdaa368a020938d267a4c41f1bc4d5a46938`.
Maix Cube остаётся compile-conformance-only и этим результатом физически не
квалифицируется.

## Правило повторного тестирования

- Всегда: полный automated CI и boot sanity текущего closure build.
- Повторить targeted physical test, если diff затрагивает соответствующий
  capability/provider/HAL/routing, его composition или ресурсные assumptions.
- Повторить более широкий набор только для board, toolchain или неясного
  cross-cutting изменения.
- UI/app-only diff не обнуляет UART, I2C, TIME или другие независимые
  observations.
- Старый результат не переписывается: carry-forward требует явного impact
  review.

## Принятые результаты

| Область | Физический результат |
| --- | --- |
| Boot/debug | SEN0305 загружается как 0.4.0; `HKPING` возвращает `HKPONG` |
| TIME | `ticks_ms`, sleep target и bounded cancellation прошли |
| UART loopback | Повторные byte-distinct передачи, cancellation и чистая post-cancel передача прошли |
| Native/HMPY restore | HMPY работает до и после MicroPython raw use/cancellation без reboot |
| I2C controller | Nano SuperMini target `0x42`: read/write, разные prefix lengths, NACK recovery и stress прошли |
| Camera/display | Camera, menu, Files, Pong и MicroPython UI визуально проверены; title residue и UI overlap устранены |
| Buttons | LEFT/OK/RIGHT/BACK прошли press, hold, release и no-repeat через `BUTTON TEST` |
| Files/SD | Папки и файлы открываются, запись и удаление временного файла работают |
| MicroPython | Выбор файла, VIEW CODE, RUN, STOP, повторный RUN, logs/startup и cleanup работают |
| Lights | Backlight, illumination, RGB, safe-off cleanup и persisted restore работают |
| Regression | Menu, Pong, Sleep, camera, SD/Files и boot прошли targeted проверки |

Финальные изменения runtime затронули Buttons, Files и MicroPython UI. Они были
повторно проверены владельцем устройства; Lights также получил ранее
отсутствовавшую проверку safe-off/restore. UART, I2C, TIME, camera, menu, Pong,
Sleep и HMPY перенесены после impact review: provider/HAL/routing для них не
изменялись. Exact implementation commit `f8b76f441d25a7be02bf8c804736750306b8f2b7`
прошёл [Release firmware run 32717823567](https://github.com/aztechell/hackylens/actions/runs/32717823567).

## Явные ограничения evidence

- Исторические численные button latency samples и matched-workload display
  dataset не реконструировались и не заменялись синтетическими цифрами.
- Независимый ISP flash readback не записан; identity связывается через exact
  candidate build, flash и boot sanity устройства.
- Физическая квалификация второй K210-платы не заявляется.

Machine-readable owner acceptance находится в
`docs/evidence/phase2-hardware-smoke.json`; immutable automated result и closure
chain — в `docs/evidence/phase2-candidate-result.json` и
`docs/evidence/phase2-closure-result.json`.
