#include "apphelp.h"

QString applicationHelpText(const QString &language) {
    if (language == QStringLiteral("uk")) {
        return QString::fromUtf8(R"HELP(FobosAPP: короткий практичний довідник

Призначення
FobosAPP - SDR-програма для Fobos SDR, Fobos Agile, RTL-SDR, rtl_tcp, експериментального нативного bladeRF RX і SoapySDR backend. Вона поєднує прийом IQ, спектр, водоспад, аудіодемодуляцію, сканування, записи, пресети, GNSS/QTH-мапу, мережевий режим і дослідні цифрові декодери.

Основна логіка частот
- Central Frequency - центральна частота приймача, тобто центр видимої IQ-смуги.
- Listening Frequency - частота прослуховування/маркер демодулятора всередині поточної смуги.
- Bandwidth - аудіо/канальна смуга для демодулятора.
- У RF-режимі маркер має бути всередині центральна частота +/- половина sample rate.
- У прямому HF-режимі центральна RF-частота не використовується так само, як в RF-режимі.

Миша, жести і швидке налаштування
- Колесо миші над спектром або водоспадом змінює масштаб видимого діапазону.
- Середня кнопка миші/клік колесом по сигналу на спектрі або водоспаді автоматично ставить маркер на центр найближчого видимого сигналу.
- Подвійний лівий клік по спектру або водоспаду робить те саме автоцентрування.
- Правий клік по спектру або водоспаду відкриває меню: поставити центр сигналу, поставити край USB/LSB або перенести центральну частоту приймача.
- Ліве перетягування по спектру показує вимірювання ширини сигналу.
- Після виділення смуги кнопка Вузький спектр біля Пресетів відкриває окремий детальний аналізатор. Повний опис є нижче у розділі Вузькосмуговий Zoom FFT.
- Fine tune шкала або круглий регулятор рухає частоту прослуховування малими кроками.
- F9 працює як тангента запису: утримуйте для запису, відпустіть для зупинки.

Приймачі
- Fobos Standard - основний режим для стандартної прошивки.
- Fobos Agile - підтримує швидкий firmware scan і live-переналаштування.
- RTL-SDR native спершу використовує rtlsdr\\rtlsdr.dll і сумісний rtlsdr\\libusb-1.0.dll; DLL у корені є тільки запасним варіантом.
- bladeRF native використовує bladerf\\bladeRF.dll у Windows-пакеті або системну libbladeRF. Це експериментальний RX-only шлях без SoapySDR.
- rtl_tcp підключається до 127.0.0.1:1234.
- SoapySDR backend доданий як теоретична сумісність, якщо на системі є SoapySDR.dll і модулі приймача.
- Для Fobos типовий sample rate - 50 MHz. Для RTL типовий безпечний sample rate - 2.048 MHz.

Спектр і водоспад
- Spectrum/waterfall update у загальних налаштуваннях задає інтервал оновлення. Auto обирає безпечний режим.
- Waterfall speed додає кількість рядків на один FFT-кадр: це робить водоспад візуально швидшим без додаткового FFT-навантаження.
- FFT length впливає на деталізацію і навантаження. Доступні також 33554432 і 67108864. Режим Гц/точку обчислює точну парну довжину FFT зі sample rate; після його вимкнення програма обирає найближче стандартне значення. Великі FFT можуть вимагати кількох гігабайтів пам'яті та повільнішого оновлення.
- FFT backend у загальних налаштуваннях обирає CPU FFTW або експериментальний Vulkan VkFFT. Auto один раз порівнює CPU/GPU для довжин від 1048576 до 8388608 і лишає швидший варіант; для ще більших FFT одразу використовує багатопотоковий FFTW, щоб не блокувати інтерфейс довгим тестом. Явний GPU режим доступний для всіх підтримуваних довжин, а помилка GPU автоматично повертає FFTW.
- Band markers показують діапазони: загальні радіодіапазони, аматорські діапазони або компактний шар.
- Spur suppression/Spur calibration допомагає позначати і приглушувати стабільні внутрішні спури.
- У Вимірі спектра A/B-маркери задають межі наукового розрахунку. Shift + ліва кнопка на звичайному спектрі ставить активний маркер; кнопки Peak, Попер. і Наст. переходять між локальними піками.
- Max hold, Min hold та Average показують утримувані криві. Average усереднює лінійну потужність, а не значення децибелів. Рядок метрик показує шумову підлогу, SNR, сумарну потужність, OBW90/95/99, ширину за -3/-6/-20 dB та ACPR; Звіт експортує дані у CSV.
- Вкладка Аналізатор спектра у вікні Дослідження керує детектором Sample/Peak/RMS/Average/Median/Quasi-peak, VBW, перекриттям FFT 0/25/50/75%, скінченним усередненням та процентильними трасами P50/P90/P99. RBW розраховується з ширини біна та ENBW вибраного FFT-вікна.
- Zero Span будує часову залежність рівня для частоти прослуховування, маркера A/B або поточного піку. Смуга виміру інтегрує потужність навколо частоти; нуль бере найближчий відображений бін.
- Тригер Off дає безперервне рухоме вікно, Auto позначає перетини порога, а Normal після Готовність зберігає задану частину даних до тригера, добирає дані після нього і зупиняє трасу. Дані експортуються у CSV з часовими мітками.
- Дослідження відкриває окреме вікно з аналізом гребінок/гармонік, статистикою сигналу, осцилограмою та сузір'ям IQ, автокореляцією і порівнянням двох КХ входів.
- У HF interference lab кнопка Аналіз відкриває ту саму вкладку завад. Записати еталон фіксує стан «до», після чого таблиця показує зміну рівня кожного піка; аналізатор розділяє до трьох гребінок, оцінює дрейф кроку й амплітуди та показує орієнтовний клас джерела. Це діагностична підказка, а не точна ідентифікація пристрою.
- Вкладка двох КХ входів використовує сирі HF1/HF2 та показує різницю рівнів, кореляцію, затримку, фазу на частоті прослуховування і орієнтовний потенціал придушення. У RF-режимі пара чисел є квадратурними I/Q, тому порівняння двох входів там навмисно вимкнене.

Вузькосмуговий Zoom FFT
- Спочатку виділіть потрібну смугу лівим перетягуванням на основному спектрі, потім натисніть Вузький спектр у рядку біля кнопки Пресети.
- Окреме вікно приймає живий повносмуговий IQ, цифровим змішувачем переносить лише виділену смугу до нуля, фільтрує та зменшує sample rate. Тому основний спектр і аудіо продовжують працювати незалежно.
- Гц/точку задає частотну роздільну здатність вузького FFT. Менше значення дає більше деталей, але потребує довшого початкового накопичення: фізична межа становить приблизно 1 / (Гц/точку) секунди.
- Оновлення задає часовий крок між перекритими FFT-кадрами, а не тривалість самого FFT. Наприклад, після першого накопичення режим 1 Гц/точку може оновлюватися значно частіше ніж раз на секунду завдяки overlap.
- Вікно має власні рівні спектра й водоспаду, швидкість водоспаду та збереження налаштувань. Воно аналізує тільки частоти всередині поточного IQ-вікна приймача.
- Під час сканування або переналаштування центральної частоти вибрана смуга має залишатися всередині поточного IQ-вікна; інакше дані для неї тимчасово відсутні.

3D-водоспад
- У розділі 3D waterfall можна обрати звичайний 2D-водоспад, 3D або 3D з мініатюрою 2D-водоспаду.
- Альтернативний інтерфейс у загальних налаштуваннях ховає окремий спектр і показує його прозорий контур перед фіксованим 3D-водоспадом. Камера і звичайний 2D-режим при цьому заблоковані, але доступні 3D та 3D з малим водоспадом зліва вгорі; шкала частот розташована знизу.
- Галочка Зафіксувати площину застосовує ту саму фронтальну 3D-проєкцію у звичайному інтерфейсі та блокує керування камерою. Градієнт у цьому режимі заповнює видимий передній торець моделі.
- Нижня спектральна область альтернативного інтерфейсу зберігає вимір смуги лівим перетягуванням, підказку частоти/рівня, автоцентрування та контекстне меню. Підписи діапазонів також переносяться на цей шар; градієнт під контуром вмикається окремою галочкою, а його прозорість задається слайдером.
- Роздільна здатність 1/1...1/64 визначає кількість частотних точок у 3D. Для дробних значень сусідні точки усереднюються, а не просто відкидаються.
- Пам'ять задає кількість рядків історії у 3D-моделі. Великі значення потребують більше GPU-пам'яті й часу на відмальовування.
- Ctrl + колесо наближає або віддаляє 3D-камеру. Ctrl + перетягування лівою кнопкою обертає камеру, Ctrl + правою кнопкою рухає її паралельно площині водоспаду.
- Alt або Shift + утримання лівої кнопки показує частотний зріз. Колесо при утриманні пересуває зріз; його крок і ширина до 4096 точок задаються у налаштуваннях 3D. У Linux ліва Alt може перехоплюватися робочим столом: використовуйте праву Alt або Ctrl+Alt зліва.
- Alt або Shift + утримання правої кнопки показує часовий зріз спектра. Колесо пересуває його за заданим кроком, а параметр рядків визначає ширину зрізу. Для Linux так само надійні права Alt або Ctrl+Alt.
- Якщо VNC не передає Alt/Shift, увімкніть VNC-керування зрізами: тоді ліва/права кнопка працюють зі зрізами без клавіатури. Вимкніть галочку, щоб повернути звичайне налаштування частоти, перетягування та контекстне меню.
- Без галочки Захват часовий зріз залишається на обраному місці, а дані в ньому оновлюються. Захват веде вибраний епізод разом із рухом історії. Зафіксувати захват додатково заморожує копію вибраного епізоду для розглядання.
- Ці самі 3D-режими, камера і зрізи доступні у вікні перегляду spectrum-frame записів.

)HELP") + QString::fromUtf8(R"HELP(

Калібрування
- Frequency calibration offset у загальних налаштуваннях компенсує сталу похибку частоти приймача і додається до апаратного налаштування.
- Amplitude calibration offset коригує показані рівні спектра та вимірювання у dB; на IQ і аудіодемодуляцію він не впливає.
- Presets -> Калібрування зберігає таблицю точок частота/частотна поправка/амплітудна поправка. Між точками поправки лінійно інтерполюються, а поза діапазоном використовується найближча крайня точка; глобальні офсети додаються до табличних.
- Reset calibration offsets повертає обидва значення до нуля.

Аудіо і демодуляція
- Audio вмикає локальне прослуховування.
- Modulation обирає AM, FM/NFM/WFM, SSB, CW, DMR та інші режими.
- Audio LPF/HPF у налаштуваннях фільтрує аудіо після демодуляції.
- Для SSB можна через правий клік поставити край USB/LSB по видимому сигналу.
- Якщо звук не відповідає сигналу після швидких переналаштувань, натисніть Stop/Start або змініть центральну частоту ще раз. Це має бути рідкісний аварійний сценарій.

Сканування
- Agile scan працює тільки з Fobos Agile firmware і сканує список діапазонів на рівні прошивки.
- Standard scan працює через послідовне live-переналаштування центральної частоти. Він доступний для Fobos, RTL, rtl_tcp і Soapy, якщо backend підтримує retune.
- У Standard scan користувач задає список центральних частот у MHz. Програма автоматично розсуває центри мінімум на sample rate, щоб смуги не накладалися.
- Кнопки +/- додають або прибирають сусідні центри відносно найменшого/найбільшого значення.
- Fill range створює список центрів з початкової і кінцевої частоти.
- Dwell ms - скільки часу слухати одну центральну частоту.
- Settle ms - пауза після переналаштування, щоб старі IQ-дані не змішувались з новими.
- Listening scan не рухає центральну частоту, а перебирає частоти прослуховування всередині видимої смуги. Це корисно для GNSS L1, FT8, маяків і будь-яких наборів частот у межах одного IQ-вікна.
- Lock listening frequency у скані фіксує частоту прослуховування, щоб маркер не стрибав між діапазонами.
- Measure у скані збирає current/peak/baseline/delta по бінових ділянках спектра.

Пресети
- Presets відкриває менеджер частот, аудіосмуг, Agile scan, Standard scan, Listening scan, band markers і QTH markers.
- Стрілки вгору/вниз у менеджері пресетів змінюють порядок показу.
- Налаштування зберігаються у профілі користувача, а не поряд із програмою, тому оновлення їх не стирає і права адміністратора не потрібні. Експорт FobosAPP.ini використовуйте для резервної копії або перенесення на інший комп'ютер.
- Import/Export settings у загальних налаштуваннях робить резервну копію або повертає збережені налаштування.

GNSS, GPS і QTH
- GPS/QTH блок зберігає своє місцеположення, показує Maidenhead/QTH locator і відкриває карту.
- QTH Map підтримує автономну сітку, локальні XYZ-тайли z/x/y.png, онлайн OpenStreetMap, MapTiler, Mapbox, NASA GIBS і власний XYZ URL. Онлайн-тайли можна кешувати на диску або тримати тільки в пам'яті.
- Колесо миші масштабує карту навколо курсора, перетягування мишею рухає карту.
- Правий клік по карті може ставити користувацький маркер; правий клік по маркеру видаляє його.
- Маркери можна редагувати у Presets -> QTH markers.
- Оверлей карти перемикає QTH-сітку та локально масштабовані супутники; карта неба показує азимут, кут підйому, C/N0 і використання супутника у fix.
- Зовнішній GNSS-модуль підключається через вибраний COM/tty порт. Для більшості NEO-M8N типовий baud rate 9600; валідні NMEA GGA/RMC або UBX NAV-PVT автоматично оновлюють QTH.
- Політика Auto / UBX пріоритет / Тільки NMEA / Тільки UBX визначає, який потік може оновлювати позицію. Авто UBX запитує NAV-PVT, NAV-SAT і NAV-DOP після відкриття сумісного u-blox модуля.
- Вікно Супутники показує живий статус і карту неба, а окрема таблиця NMEA GSV/GSA та UBX NAV-SAT сортується за колонками. Чекбокси й команди правої кнопки дозволяють включати або ігнорувати окремі системи та супутники у відображенні й аналізі; готові координати самого модуля від цього не перераховуються.
- UBX сист. опитує/застосовує CFG-GNSS для GPS/GLO/GAL/BDS/QZSS/SBAS, а Save cfg просить модуль зберегти конфігурацію у підтримувану енергонезалежну пам'ять.
- Raw записує бінарний UBX/NMEA потік, NMEA log/replay записує або повторно подає текстові речення через той самий парсер. Ці файли можуть містити реальні координати й навмисно не входять до релізу.
- Tune GNSS L1 і GNSS scan виставляють частоти для GPS/Galileo/BeiDou/GLONASS L1.
- Accum виконує один накопичувальний GPS L1 C/A пошук, Auto безперервно аналізує rolling IQ, Replay запускає той самий acquisition на Channel-IQ WAV, а Self-test перевіряє синтетичний acquisition/позиційний тракт. Теплова карта показує PRN, Doppler, code phase і історію піків. Для реального lock потрібні достатній сигнал, стабільна частота і хороша GNSS-антена.
- IQ monitor вимірює рівень, DC offset, clipping та I/Q balance до acquisition; Save GNSS IQ зберігає поточний stereo IQ snapshot і контекст налаштування.

Цифрові режими
- Digital Audio містить DMR-дослідний декодер. Він може визначати частину метаданих і працювати з зовнішнім voice backend, але DMR-голос поки експериментальний.
- DMR backend обирає FobosAPP+mbelib, FobosAPP+OpenDMR/OP25 або DSD-neo; DSD-neo передає DMR PCM дискримінатора через TCP і може приймати декодоване аудіо назад через UDP.
- Lock DMR фіксує обрані параметри DMR, корисно коли на частоті є різні color code/timeslot/contact.
- DMR hunter, FPV hunter і Digital Video hunter шукають характерні сигнали на спектрі за шириною/порогом.
- Video блок зараз дослідний; повноцінні відеодекодери можуть бути вимкнені у збірці.

Запис і відтворення
- Record пише Audio WAV або Channel IQ WAV залежно від вибраного режиму.
- Розширені метадані запису в загальних налаштуваннях додають до WAV/JSON і spectrum-event записів версію програми, тракт приймача, FFT/RBW, калібрування, маркери A/B та наукові вимірювання. Координати, серійні номери, ключі й API-токени навмисно не записуються.
- Hold F9/F9 утримання - швидкий momentary recording.
- Playback дозволяє програти сумісні записи, коли приймач зупинений.
- Spectrum-frame recorder постійно тримає ввімкнений передбуфер, а після команди захоплення зберігає подію до ручної зупинки. Режими Channel IQ і Full IQ додатково дозволяють демодулювати запис у вікні реплею.
- У spectrum replay маркер часу синхронно керує спектром, водоспадом та аудіо; Play запускає їх із поточної позиції. Full IQ дозволяє рухати частоту прослуховування, Channel IQ лишається прив'язаним до записаного каналу.
- Реальні IQ-записи можуть бути великими; перед релізом не додавайте особисті записи, логи і тестові ефіри у репозиторій.

Мережа
- Network відкриває налаштування server/client режиму.
- Server може передавати керування, спектр, аудіо або IQ залежно від режиму.
- Після підключення клієнт отримує авторитетний список приймачів сервера і додає їх у свій список як [Server] .... До отримання цього списку клієнт не надсилає серверу локальний індекс приймача; Refresh лише повторно запитує кешований список і не перепідключає USB-пристрій.
- Обраний серверний приймач відкривається і контролюється на сервері. Спостерігач не може змінювати параметри, доки не отримає пріоритет керування.
- Full-IQ/Channel-IQ режими важкі для мережі і CPU, тому їх краще тестувати поступово.
- Audio relay і Audio HTTP stream дозволяють передавати аудіо в інші програми або на інші пристрої.

Корисні правила
- Якщо щось виглядає дивно після зміни приймача або sample rate, натисніть Stop, перевірте backend/sample rate і запустіть знову.
- Для слабких сигналів спершу добийтеся стабільного спектра і правильного центру, а вже потім ускладнюйте демодуляцію.
- Для RTL не ставте занадто великий sample rate: 2.048 або 2.4 MHz зазвичай безпечніші.
- Для Fobos Agile дуже малі інтервали live retune можуть перевантажувати USB/UI на слабкому комп'ютері. Збільшуйте Agile live retune interval, якщо бачите зависання.
- Докладне логування вмикайте тільки на час тесту: воно корисне для діагностики, але може робити програму важчою.
)HELP");
    }

    return QString::fromUtf8(R"HELP(FobosAPP: practical user guide

Purpose
FobosAPP is an SDR application for Fobos SDR, Fobos Agile, RTL-SDR, rtl_tcp, experimental native bladeRF RX and an experimental SoapySDR backend. It combines IQ reception, spectrum, waterfall, audio demodulation, scanning, recordings, presets, GNSS/QTH mapping, network mode and experimental digital decoders.

Frequency model
- Central Frequency is the SDR receiver center, the middle of the visible IQ span.
- Listening Frequency is the demodulator marker inside the current span.
- Bandwidth is the demodulator/audio channel width.
- In RF mode the listening marker must stay inside center frequency +/- half of the sample rate.
- In direct HF modes the RF center is not used the same way as in RF mode.

Mouse actions and fast tuning
- Mouse wheel over the spectrum or waterfall changes visible span/zoom.
- Middle-click, usually clicking the mouse wheel, on a signal in the spectrum or waterfall snaps the listening marker to the nearest visible signal center.
- Double left-click on the spectrum or waterfall does the same auto-centering.
- Right-click on the spectrum or waterfall opens a tuning menu: tune signal center, set USB/LSB edge, or move receiver center here.
- Left-drag on the spectrum shows a bandwidth measurement.
- After selecting a band, the Zoom spectrum button next to Presets opens a dedicated detailed analyzer. See Narrow-band Zoom FFT below for the full workflow.
- The fine-tune scale or round dial nudges the listening frequency in small steps.
- F9 works as a push-to-record key: hold to record, release to stop.

Receivers
- Fobos Standard is the main mode for the standard firmware.
- Fobos Agile supports firmware scan and live retuning.
- RTL-SDR native first uses rtlsdr\\rtlsdr.dll and the matching rtlsdr\\libusb-1.0.dll; root-folder DLLs are only a fallback.
- bladeRF native uses the bundled bladerf\\bladeRF.dll runtime in the Windows beta package, or a system libbladeRF installation. It is an RX-only experimental path without SoapySDR.
- rtl_tcp connects to 127.0.0.1:1234.
- SoapySDR is added as theoretical compatibility when SoapySDR.dll and device modules are installed.
- The default Fobos sample rate is 50 MHz. The safe RTL default is 2.048 MHz.

Spectrum and waterfall
- Spectrum/waterfall update in Settings controls redraw interval. Auto keeps a safe FFT-dependent default.
- Waterfall speed adds more rows per FFT frame. It makes the waterfall visually faster without increasing FFT load.
- FFT length controls frequency detail and CPU load. 33554432 and 67108864 are also available. Hz/point mode computes an exact even FFT length from the sample rate; disabling it selects the nearest standard value. Very large FFTs can require several gigabytes of memory and slower updates.
- FFT backend in Settings selects CPU FFTW or experimental Vulkan VkFFT. Auto benchmarks CPU/GPU once for lengths from 1048576 through 8388608 and keeps the faster path; above that range it uses multithreaded FFTW directly to avoid a long UI-stalling benchmark. Explicit GPU remains available for every supported length, and any GPU error automatically falls back to FFTW.
- Band markers show general radio bands, amateur bands, or a compact combined layer.
- Spur suppression and calibration can mark and reduce stable internal spurs.
- In Spectrum measurement, A/B markers define the scientific calculation range. Shift + left-click on the normal spectrum sets the active marker; Peak, Prev and Next move between local peaks.
- Max hold, Min hold and Average draw persistent traces. Average uses linear power rather than averaging decibels. The metrics row reports noise floor, SNR, integrated power, OBW90/95/99, -3/-6/-20 dB width and ACPR; Report exports the frame and metrics to CSV.
- The Spectrum analyzer tab in Research controls Sample/Peak/RMS/Average/Median/Quasi-peak detection, VBW, 0/25/50/75% FFT overlap, finite averaging and P50/P90/P99 traces. RBW is calculated from bin width and the selected FFT window ENBW.
- Zero Span plots level versus time for the listening frequency, marker A/B, or the current peak. Measurement bandwidth integrates power around the target; zero uses the nearest displayed bin.
- Trigger Off provides a continuous rolling window, Auto marks threshold crossings, and Normal captures the configured pre-trigger portion, completes the post-trigger interval, then freezes the trace. CSV includes absolute and relative timestamps.
- Research opens a separate window for comb/harmonic analysis, signal statistics, IQ waveform/constellation/autocorrelation, and dual-HF-input comparison.
- Analysis in HF interference lab opens the same interference tab. Capture reference stores the “before” spectrum so the peak table can report every level change; the analyzer separates up to three comb families, tracks spacing and amplitude drift, and reports a tentative source class. The source class is a diagnostic hint, not exact device identification.
- Dual HF inputs uses raw HF1/HF2 data and reports level difference, correlation, lag, phase at the listening frequency, and estimated cancellation potential. In RF mode the pair is quadrature I/Q, so dual-input comparison is intentionally disabled.

Narrow-band Zoom FFT
- First select a band by left-dragging on the main spectrum, then click Zoom spectrum in the row next to Presets.
- The separate window consumes live full-span IQ, digitally shifts only the selected band to zero, filters it, and reduces its sample rate. The main spectrum and audio therefore continue independently.
- Hz/bin selects narrow-FFT frequency resolution. A smaller value reveals more detail but needs a longer initial acquisition; the physical limit is approximately 1 / (Hz/bin) seconds.
- Update selects the time step between overlapping FFT frames, not the duration of one FFT. After initial acquisition, 1 Hz/bin can therefore update much faster than once per second through overlap.
- The window has independent spectrum/waterfall levels, waterfall speed, and persistent settings. It can analyze only frequencies inside the receiver's current IQ span.
- During scanning or center retuning, the selected band must remain inside the current IQ span; otherwise its data is temporarily unavailable.

3D waterfall
- The 3D waterfall section selects the normal 2D waterfall, 3D, or 3D with a small 2D waterfall overlay.
- Alternative interface in Settings hides the separate spectrum and overlays its transparent contour at the front of a fixed 3D waterfall. Camera controls and normal 2D mode are locked, but both 3D and 3D with a top-left mini waterfall remain available; the frequency scale is placed below the scene.
- Fix plane applies the same front-facing 3D projection in the normal interface and locks camera controls. In this mode, the gradient option fills the visible front face of the model.
- Its lower spectrum area retains left-drag bandwidth measurement, frequency/level hover data, auto-centering and the tuning context menu. Band labels are preserved there, while a checkbox enables gradient fill below the contour and a slider controls its opacity.
- Resolution 1/1...1/64 controls the number of frequency points in 3D. Fractional modes average neighboring points instead of merely discarding them.
- Memory controls the number of history rows in the 3D model. Larger values require more GPU memory and rendering time.
- Ctrl + wheel moves the 3D camera closer or farther. Ctrl + left-drag orbits the camera; Ctrl + right-drag pans it parallel to the waterfall plane.
- Alt or Shift + hold left button displays a frequency slice. The wheel moves the slice while held; step and width up to 4096 points are configured in the 3D section. On Linux, the desktop may reserve left Alt: use right Alt or left Ctrl+Alt.
- Alt or Shift + hold right button displays a time/spectrum-row slice. The wheel moves it by the configured step and the row setting controls its width. Right Alt or left Ctrl+Alt are the reliable Linux variants.
- If VNC does not forward Alt/Shift, enable VNC slice control: left/right buttons then operate slices without a keyboard modifier. Disable it to restore normal tuning, panning and the context menu.
- With Capture disabled, the time slice stays at the selected place while its data changes. Capture follows the selected episode through the scrolling history. Fix capture additionally freezes a copy of that episode for inspection.
- The same 3D modes, camera and slice tools are available in the spectrum-frame replay window.

)HELP") + QString::fromUtf8(R"HELP(

Calibration
- Frequency calibration offset in Settings compensates a stable receiver frequency error and is added to hardware tuning.
- Amplitude calibration offset corrects displayed spectrum and measurement levels in dB; it does not change IQ or audio demodulation.
- Presets -> Calibration stores frequency/frequency-correction/amplitude-correction points. Corrections are linearly interpolated between points, the nearest endpoint is used outside the table, and global offsets are added to table values.
- Reset calibration offsets returns both values to zero.

Audio and demodulation
- Audio enables local playback.
- Modulation selects AM, FM/NFM/WFM, SSB, CW, DMR and other modes.
- Audio LPF/HPF in settings filters demodulated audio.
- In SSB modes, right-click can align the USB or LSB edge to a visible signal.
- If audio does not match the visible signal after aggressive retuning, use Stop/Start or retune once more. This should be a rare recovery path.

Scanning
- Agile scan works only with Fobos Agile firmware and scans ranges inside the firmware.
- Standard scan works by live-retuning the receiver center through a list. It is available for Fobos, RTL, rtl_tcp, bladeRF and Soapy when the backend supports retune.
- In Standard scan, enter center frequencies in MHz. The app keeps centers at least one sample rate apart to avoid overlapping spans.
- +/- buttons add or remove neighboring centers from the low or high side.
- Fill range generates a center list between the start and end frequencies.
- Dwell ms is how long one center is observed.
- Settle ms is the pause after retune, used to keep old IQ blocks from mixing with new ones.
- Listening scan does not move the receiver center; it cycles listening frequencies inside the visible span. It is useful for GNSS L1, FT8, beacons and any fixed channel list inside one IQ window.
- Lock listening frequency keeps the listening marker fixed while scan centers move.
- Measure in scan mode collects current, peak, baseline and delta values for spectral coverage checks.

Presets
- Presets opens the manager for center frequencies, listening frequencies, audio bandwidths, Agile scan, Standard scan, Listening scan, band markers and QTH markers.
- Up/down arrows in the preset manager change display order.
- Settings are stored in the user profile rather than beside the executable, so updates preserve them and administrator rights are not required. Export FobosAPP.ini for backup or transfer to another computer.
- Import/Export settings in Settings creates or restores a settings backup.

GNSS, GPS and QTH
- GPS/QTH stores your position, shows Maidenhead/QTH locator and opens the map.
- QTH Map supports the offline grid, local z/x/y.png XYZ tiles, online OpenStreetMap, MapTiler, Mapbox, NASA GIBS and a custom XYZ URL. Online tiles may use disk cache or memory-only mode.
- Mouse wheel zooms the map around the cursor; dragging pans the map.
- Right-click on the map can place a user marker; right-click on a marker removes it.
- Markers can be edited in Presets -> QTH markers.
- Map overlay selects the QTH grid and locally scaled live satellites; the sky view shows azimuth, elevation, C/N0 and whether each satellite participates in the module fix.
- An external GNSS module connects through the selected COM/tty port. Most NEO-M8N boards default to 9600 baud; valid NMEA GGA/RMC or UBX NAV-PVT fixes update QTH automatically.
- Auto / UBX preferred / NMEA only / UBX only controls which stream may update position. Auto-enable UBX requests NAV-PVT, NAV-SAT and NAV-DOP after opening a compatible u-blox module.
- The Satellites window shows live status and sky view; the separate NMEA GSV/GSA and UBX NAV-SAT table can be sorted by column. Checkboxes and right-click bulk actions include or ignore systems and satellites in app display/analysis; they do not recompute the position already solved by the module.
- UBX sys polls/applies CFG-GNSS for GPS/GLO/GAL/BDS/QZSS/SBAS, while Save cfg asks the module to persist its configuration to supported non-volatile memory.
- Raw records the binary UBX/NMEA stream; NMEA log/replay records or feeds text sentences through the same parser. These files may contain real coordinates and are intentionally excluded from release packages.
- Tune GNSS L1 and GNSS scan set receiver frequencies for GPS/Galileo/BeiDou/GLONASS L1.
- Accum runs one accumulated GPS L1 C/A search, Auto continuously analyzes rolling IQ, Replay runs the same acquisition on a Channel-IQ WAV, and Self-test validates the synthetic acquisition/position path. The heatmap displays PRN, Doppler, code phase and peak history. A real lock needs enough signal, stable tuning and a proper GNSS antenna.
- IQ monitor measures level, DC offset, clipping and I/Q balance before acquisition; Save GNSS IQ stores the current stereo IQ snapshot and tuning context.

Digital modes
- Digital Audio contains the experimental DMR decoder. It can detect some metadata and work with an external voice backend, but DMR voice is still experimental.
- DMR backend selects FobosAPP+mbelib, FobosAPP+OpenDMR/OP25 or DSD-neo; DSD-neo mirrors DMR discriminator PCM over TCP and can receive decoded audio back over UDP.
- Lock DMR fixes selected DMR parameters, useful when several color code/timeslot/contact combinations share a frequency.
- DMR hunter, FPV hunter and Digital Video hunter look for characteristic signals by width and threshold.
- Video is currently experimental; full video decoders may be disabled in the build.

Recording and playback
- Record writes Audio WAV or Channel IQ WAV depending on selected recording mode.
- Extended recording metadata in Settings adds the app version, receiver path, FFT/RBW, calibration, A/B markers and scientific measurements to WAV/JSON and spectrum-event recordings. Coordinates, device serials, keys and API tokens are intentionally excluded.
- Hold F9 is quick momentary recording.
- Playback can play compatible recordings while the receiver is stopped.
- Spectrum-frame recorder continuously keeps an enabled pre-trigger buffer, then saves the event until capture is stopped. Channel IQ and Full IQ modes also support demodulation inside the replay window.
- In spectrum replay, the time marker controls spectrum, waterfall and audio together; Play starts all of them at the current position. Full IQ permits moving the listening frequency, while Channel IQ stays tied to its recorded channel.
- Real IQ recordings can be huge; avoid adding personal recordings, logs and over-the-air tests to repository releases.

Network
- Network opens server/client settings.
- Server mode can share control, spectrum, audio or IQ depending on processing mode.
- After connection, the client receives the authoritative server receiver list and adds entries as [Server] .... Until that list arrives, the client does not send its local receiver index to the server; Refresh only requests the cached list again and does not reopen the USB device.
- The selected remote receiver is opened and owned by the server. An observer cannot change parameters until it receives control priority.
- Full-IQ and Channel-IQ modes are heavy for network and CPU, so test them gradually.
- Audio relay and Audio HTTP stream can send demodulated audio to other programs or devices.

Useful rules
- If something looks wrong after changing receiver or sample rate, press Stop, check backend/sample rate and start again.
- For weak signals, first get a stable spectrum and correct center, then tune the demodulator.
- For RTL, avoid excessive sample rates. 2.048 or 2.4 MHz are usually safer.
- On Fobos Agile, very small live-retune intervals can overload USB/UI on slower computers. Increase Agile live retune interval if you see stalls.
- Enable detailed logging only while testing. It is valuable for diagnosis but can make the app heavier.
)HELP");
}
