# Результаты PoC futex/signal на GNU Hurd

См. `PLAN.md` для контекста и целей.

## Как воспроизвести

`.github/workflows/run-hurd-poc.yml` (workflow_dispatch): собирает образ из
`hurd-image-parts/`, монтирует ext2-раздел через loop-device (rw), кладёт
`poc/*.c` в `/root/poc`, патчит `grub.cfg` (serial-консоль + убирает
`--readonly` у `ext2fs.static`), затем поднимает образ в QEMU
(`-nographic`-эквивалент, `-serial stdio`, TCG без KVM) и через
`.github/scripts/run_poc.py` (pexpect) логинится root'ом (пароль пустой),
собирает `poc/Makefile` и прогоняет оба бинарника. Полная стенограмма serial-
консоли сохраняется как артефакт `hurd-poc-transcript`.

Итоговый успешный прогон: https://github.com/unxed/debian-hurd/actions/runs/35337090041

## futex-подобная синхронизация: gsync_wait/gsync_wake

GNU Mach **не** реализует OSF/XNU-style `semaphore_create/wait/signal`
(отсутствуют в заголовках — `mach.h` их не декларирует). Вместо этого в
`<mach/gnumach.h>` есть нативный примитив, семантически идентичный Linux
futex:

```c
kern_return_t gsync_wait(task_t task, vm_address_t addr,
                          unsigned val1, unsigned val2,
                          natural_t msec, int flags);
kern_return_t gsync_wake(task_t task, vm_address_t addr,
                          unsigned val, int flags);
kern_return_t gsync_requeue(task_t task, vm_address_t src_addr,
                             vm_address_t dst_addr,
                             boolean_t wake_one, int flags);
```

`gsync_wait` атомарно проверяет, что 32-битное слово по `addr` равно `val1`;
если нет — возвращается немедленно (не `KERN_SUCCESS`), если да — блокируется
до `gsync_wake` по тому же адресу. Ровно `FUTEX_WAIT`/`FUTEX_WAKE`. Флаги:
`GSYNC_SHARED` (адрес общий между задачами, а не task-local),
`GSYNC_BROADCAST`, `GSYNC_MUTATE`, `GSYNC_QUAD`, `GSYNC_TIMED` — но сами
числовые значения этих макросов не установлены пакетом `hurd-dev` в этом
образе (объявлены только в комментариях `.defs`); для task-local
wait/wake они и не нужны (`flags=0`).

**Результат** (`poc/futex_poc.c`, generation-counter паттерн, 2000 итераций
wait→wake между двумя pthread-потоками одного процесса):

```
RESULT: OK iters=2000 blocked=2000 immediate=0 avg_us=158.39 min_us=55.78 max_us=9866.54
```

Ни одного потерянного пробуждения. Средняя задержка ~158 мкс (макс. ~9.9 мс —
похоже на джиттер QEMU TCG без KVM, а не свойство примитива). **Вывод:
futex-подобный примитив на голом Hurd есть и работает.**

## Асинхронная доставка сигналов: SIGUSR1 без SA_RESTART

`poc/sig_poc.c`: отдельный поток раз в 2 мс шлёт `SIGUSR1` через `kill()`,
основной поток 500 раз вызывает `nanosleep(50ms)` без `SA_RESTART` в
обработчике.

```
RESULT: handler_fired=500 eintr_in_nanosleep=0 full_sleep=500 matched_pairs=500 avg_us=611.43 min_us=353.40 max_us=33522.84
```

- Обработчик отработал **все 500/500** раз — доставка сигналов надёжна.
- Средняя задержка доставки ~611 мкс (хвост до 33 мс — вероятно опять
  TCG-эмуляция single-core, `-smp 1`).
- **Но `eintr_in_nanosleep=0`**: ни один `nanosleep()` не был прерван сигналом
  досрочно, хотя `SA_RESTART` не установлен. Обработчик вызывается «поверх»
  спящего потока, но сам блокирующий syscall не возвращает `EINTR` и не
  прерывается.

**Вывод:** доставка сигналов в обработчик работает и достаточно быстрая, но
Hurd, похоже, не прерывает блокирующие syscall'ы сигналом так, как это делает
Linux/BSD (`EINTR`). Это стоит перепроверить на другом блокирующем вызове
(например, `read()` на pipe) прежде чем делать далеко идущие выводы, но если
подтвердится — это потенциальный блокер именно для моделей вытесняющего
планирования, завязанных на прерывание заблокированного syscall'а сигналом
(в духе `SIGURG`-preemption в рантайме Go), а не для сигналов как таковых.

## Итог по исходному вопросу плана

Ни futex-подобная синхронизация, ни базовая асинхронная доставка сигналов
сами по себе не выглядят фундаментальным блокером для порта на голый Hurd.
Единственный обнаруженный нюанс — отсутствие прерывания блокирующих syscall'ов
сигналом (`EINTR`) — стоит исследовать отдельно перед тем как полагаться на
него в архитектуре (а) или (б) из `PLAN.md`.
