# PoC futex/signal на GNU Hurd

## Токен
В этой сводке не указан, должен быть передан вместе с ней.

## Цель
Проверить, есть ли фундаментальный блокер в виде futex-подобной синхронизации и надёжной асинхронной доставки сигналов на голом GNU Hurd — от этого зависит фактическая оценка (обсуждали ранее в другом диалоге) для двух архитектурных идей: (а) нативный порт рантайма Go на Hurd, (б) транслятор Linux-бинарников для Hurd. Обе упираются в один и тот же вопрос: работают ли на Hurd примитивы, аналогичные futex, и приходят ли сигналы асинхронно и быстро.

## Репозиторий
`github.com/unxed/debian-hurd` (публичный), ветка `main`.

**Что уже в репо:**
- `debian-hurd-amd64-20260314.img` — собранный образ (склеен из `hurd-image-parts/part_*`, замёржен в main). MBR: p1=swap, p2=ext2-root (монтируется нативно линуксовыми `losetup -fP` + `partx -a`).
- Внутри образа в `/root/poc/` (зашито напрямую в ext2 через loop-mount, **не в git**, а в самом .img):
  - `futex_poc.c` — semaphore_create/wait/signal (Mach) как замена futex_wait/wake.
  - `sig_poc.c` — асинхронная доставка SIGUSR1 в тугом цикле без SA_RESTART (аналог того, что нужно для preemption в духе Go SIGURG).
  - `Makefile`, `README.md`.
- Внутри образа уже стоит toolchain: `gcc-15`, `make`, `build-essential`, `hurd-dev`, заголовки `/usr/include/mach/*`. **`mig` не установлен** (для текущего PoC не нужен — используются голые Mach-трапы).
- `.github/workflows/fetch-hurd-image.yml` — качает образ по URL, проверяет размер (<1.9GB), режет на чанки по 90MB (лимит GitHub на файл — 100MB), **коммитит прямо в main** (не в PR — в настройках репо выключено "Allow GitHub Actions to create and approve pull requests", поэтому `gh pr create` из workflow падает; так и оставили, пушим напрямую).



