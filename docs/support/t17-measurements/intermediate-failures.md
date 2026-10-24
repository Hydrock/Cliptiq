# Промежуточные отказы

Final runtime5380462, CI40105477768 attempt1: Linux Debug X11 wire fixture42/62 завершился `fixture atom query` (исключение в fixture thread); остальные61 passed, release/packaging в этом job не запускались. Такой же отказ описан ещё в T15. Локальный repeat30/30 green. Повторяется только упавший job после завершения run, исходный отказ не удалён. Assertions/suppressions/автоматические retries в product test suite не менялись. Причина редкого X-серверного отказа не установлена, устранение flaky behavior не заявлено.

После pin Actions обновлённый T01 clean-profile CI40104960791 attempt1: macOS ARM `--lifecycle-smoke` timeout30s. Повтор того же job на том же SHA прошёл; attempt2 success3/3. Это isolated profile CI, не чистая потребительская ВМ.

Локальный полный Windows release suite: Win32 CreateWindowStation Access denied5; после смены контекста часть тестов получила Cannot open index gate из-за ACL общего temp. Локальные новые T17 tests5/5, финальная Windows native CI debug/release/ASan green. Нельзя считать локальный полный прогон зелёным.

Исходный Linux job: https://github.com/Hydrock/Cliptiq/actions/runs/40105477768/job/120906159392 . T01 timeout: https://github.com/Hydrock/Cliptiq/actions/runs/40104960791/job/120904643205 .
