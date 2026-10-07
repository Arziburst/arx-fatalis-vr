## Цель
Игра выводит стереоскопическое изображение в Valve Index через SteamVR (OpenVR) с правильной проекцией для каждого глаза и частотой кадров шлема.

## Что нужно
- OpenVR SDK (vcpkg `openvr`), SteamVR, Valve Index
- Понимание рендер-пайплайна: `OpenGLRenderer` (`src/graphics/opengl/OpenGLRenderer.cpp`), камера (`src/game/Camera.cpp`, `SetViewMatrix` / `SetProjectionMatrix`)

## Задачи
- [ ] Подключить OpenVR: `VR_Init`, `IVRCompositor`, флаг запуска `--vr`
- [ ] Off-screen FBO для каждого глаза с разрешением из `GetRecommendedRenderTargetSize`
- [ ] Рендер сцены дважды: матрицы глаз `GetEyeToHeadTransform` + `GetProjectionMatrix`
- [ ] `WaitGetPoses` → `Submit` для обоих глаз; отключить vsync окна
- [ ] Зеркало на мониторе (один глаз) для отладки и стримов
- [ ] Разобраться с эффектами в экранных координатах (вспышки, частицы, туман, затемнения)
- [ ] Поддержка 90/120/144 Гц

## Критерии готовности
- В шлеме корректное стерео без двоения, масштаб мира визуально правдоподобный
- Стабильные 90 FPS на Index

## Зависимости
Эпик «Сборка и инфраструктура».
