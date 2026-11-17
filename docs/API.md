# Справочник API Acoustic Radar

Документация классов, функций и структур данных C++-приложения.

В непрерывном режиме callback PortAudio передаёт входные аудиокадры через кольцо в рабочий поток. Рабочий поток ищет синхромаркер, анализирует полный цикл и помещает готовые спектры в очередь. Основной поток получает результаты и обновляет спектрограмму.

- @ref acoustic_radar::AudioDevice — звуковое устройство и входное кольцо.
- @ref acoustic_radar::ChirpGenerator — синхромаркер OUTPUT 1 и возбуждение OUTPUT 2.
- @ref acoustic_radar::SpscRing — очередь с одним потоком записи и одним потоком чтения.
- @ref acoustic_radar::ProcessingWorker — рабочий поток и очередь результатов.
- @ref acoustic_radar::CycleProcessor — накопление данных и выделение циклов.
- @ref acoustic_radar::SyncDetector — корреляционный поиск маркера INPUT 1.
- @ref acoustic_radar::SpectrumAnalyzer — анализ INPUT 2 с помощью FFTW3.
- @ref acoustic_radar::Spectrogram — накопление и показ спектров через SDL3.

Структуры @ref acoustic_radar::InputFrame и @ref acoustic_radar::StereoSample передают аудиокадры. Структуры @ref acoustic_radar::SyncMarker, @ref acoustic_radar::SpectrumColumn и @ref acoustic_radar::SpectrumFrame содержат результаты синхронизации и анализа.

Комментарии к тестовым файлам описывают проверку FFT, синхронизации, кольца и восстановления после потерь. Пошаговые пояснения алгоритмов доступны при просмотре исходников.
