## Цель
Движок Arx Libertatis стабильно собирается под Windows (MSVC) и запускает оригинальную игру из Steam-папки. Сборка воспроизводима одной командой и проверяется в CI.

## Что нужно
- Visual Studio Build Tools 2022 (C++), CMake, vcpkg — **уже установлены**
- Arx Fatalis из Steam (app 1700) — **установлена** в `D:\games\SteamLibrary\steamapps\common\Arx Fatalis`
- GitHub Actions (Windows runner)

## Задачи
- [x] Склонировать Arx Libertatis, поставить зависимости через vcpkg (SDL2, OpenAL, Boost, FreeType, GLEW, GLM, zlib)
- [x] Сделать генерацию иконок необязательной (`BUILD_DATA_ICONS`)
- [ ] Починить флаги компилятора, которые попадают в `rc.exe` (ошибка RC1203)
- [ ] Собрать `arx.exe` и запустить игру с данными из Steam-папки
- [ ] Скрипт сборки (`scripts/build-windows.ps1`) и `vcpkg.json` (manifest mode)
- [ ] CI: GitHub Actions собирает Windows-сборку на каждый PR
- [ ] Документация для разработчика: как собрать и запустить

## Критерии готовности
- Свежий клон собирается одной командой
- Игра запускается, доходит до главного меню и загружает сохранение
- CI зелёный

## Зависимости
Нет — это фундамент для всех остальных эпиков.
