Документация проекта: README.md

Установка пакетов для Ubuntu 24.04:
sudo apt update

sudo apt install -y \
    build-essential \
    cmake \
    ninja-build \
    pkg-config \
    git \
    clangd \
    gdb

sudo apt install -y \
    portaudio19-dev \
    libasound2-dev \
    alsa-utils \
    libfftw3-dev

sudo apt install -y \
    sox \
    libsox-fmt-all \
    audacity \
    pavucontrol

sudo apt install -y \
    libx11-dev \
    libxext-dev \
    libxrandr-dev \
    libxcursor-dev \
    libxi-dev \
    libxfixes-dev \
    libxss-dev \
    libwayland-dev \
    libxkbcommon-dev \
    libegl1-mesa-dev \
    libgl-dev \
    libasound2-dev \
    libpulse-dev \
    libpipewire-0.3-dev \
    libdecor-0-dev

Получение исходников SDL3 (каталог ~/projects должен существовать):
cd ~/projects

git clone --branch release-3.2.0 --depth 1 \
    https://github.com/libsdl-org/SDL.git SDL3

Сборка SDL3:
cmake -S SDL3 -B SDL3/build \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr/local \
    -DSDL_SHARED=ON \
    -DSDL_STATIC=OFF

cmake --build SDL3/build -j "$(nproc)"

Установка SDL3:
sudo cmake --install SDL3/build
sudo ldconfig

Проверка установленной версии SDL3:
pkg-config --modversion sdl3

Сборка acoustic-radar:
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build

Запуск:
./build/acoustic_radar

Тесты:
ctest --test-dir build --output-on-failure
