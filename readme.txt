Документация проекта: README.md

Сборка:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

Запуск:
./build/acoustic_radar

Тесты:
ctest --test-dir build --output-on-failure
