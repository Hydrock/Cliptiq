# Простой основного Cliptiq на Windows

Стенд измеряет настоящий `Cliptiq.exe` Base: clipboard listener, SQLite,
single-instance IPC, меню/tray, настройки и сервис превью. Raven, OCR и
автоматическая сеть не включены. Это не settings-only прототип.

## Сборка и измерение

Из корня репозитория, MSVC Build Tools с CMake/Ninja и штатная pinned SQLite:

```powershell
powershell -ExecutionPolicy Bypass -File experiments/windows-idle/build.ps1 -Tests -Targets all
python experiments/windows-idle/measure.py --build-dir out/windows-idle --output out/idle-empty.json --entries 0 --seconds 30 --expect-background
python experiments/windows-idle/measure.py --build-dir out/windows-idle --output out/idle-1000.json --entries 1000 --seconds 30 --open-settings --expect-background --expect-settings-destroyed
```

Запускать в обычном незаблокированном desktop. Во время замера не трогать окна
тестового процесса. Измеритель открывает только своё окно настроек через IPC,
закрывает его стандартным WM_CLOSE и проверяет отсутствие видимых окон. Также
проверяет уничтожение контролов и возможность повторного открытия настроек.

Каждый запуск создаёт свой профиль рядом с JSON и переопределяет `LOCALAPPDATA`
только дочернего процесса: IPC и history не затрагивают обычный профиль Cliptiq.
Синтетические записи — около1KiB текста каждая. Clipboard listener активен,
но все типы захвата выключены в фикстуре, чтобы не сохранять личное содержимое
системного буфера. Стенд проверяет idle listener, не throughput копирования.
Горячие клавиши фикстуры отключены, кроме обязательной Settings (Ctrl+Alt+Shift+F11).
Автозапуск не меняется. OCR, screenshot observation и auto updates выключены.

Пять секунд стабилизации, затем30с наблюдения; GetProcessTimes и
GetProcessMemoryInfo каждые5с. В JSON сохранены все отсчёты, полный CPU delta,
hash EXE и список HWND тестового PID. CPU100% = одно логическое ядро; для
Task Manager-подобной нормализации делить на число логических CPU.

Working set включает разделяемые системные страницы; private bytes — приватная
закоммиченная память, не точное количество приватных физических страниц.
GPU/system-service memory сюда не входит. Не вызываются EmptyWorkingSet,
SetProcessWorkingSetSize или ручная очистка кэшей ради маленького числа.

Baseline игнорировал `--background`, поэтому стенд скрывал только его собственный
launcher после создания; финальная сборка запускается скрытой сама. Разница RAM
включает эффект отсутствия первого видимого окна. Запуски последовательные,
это локальные30с наблюдения, не долгосрочная статистика всех конфигураций Windows.

`idle_profile` создаёт схему через настоящее ядро Cliptiq и заполняет её через
ingest. Регрессионные тесты находятся в `tests/integration/idle_services_test.cpp`
и входят в обычный BUILD_TESTING: неизменная история не сканируется заново;
пустой IPC mailbox не захватывает write lock.

Повторение воспроизведённого отказа baseline:

```powershell
python experiments/windows-idle/measure.py --build-dir out/windows-idle-baseline --output out/background-red.json --entries 0 --seconds 1 --expect-background
```

Baseline бинарник и промежуточные сборки сохраняются локально в `out/`, не в Git.
Исходные замеры и результаты — в `docs/support/windows-idle/`.
