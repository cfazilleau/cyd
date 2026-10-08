# RER A · Joinville-le-Pont → Auber

A small departure board for the ESP32 2.8" ST7789 touch display (the USB-C
"Cheap Yellow Display" / ESP32-32E variant). It shows the next RER A trains from
**Joinville-le-Pont towards Paris (Auber)** plus **traffic disruptions**, using the
Île-de-France Mobilités **PRIM** API.

The UI is built with [LVGL 9](https://lvgl.io), the standard graphics library for
this kind of board, using custom Montserrat fonts so French accents display properly.

| Normal | Disruption | Details (tap) |
|---|---|---|
| ![](docs/board.png) | ![](docs/board_disrupted.png) | ![](docs/details.png) |

*(Rendered from the real UI code with sample data, see `tools/preview`.)*

## Behaviour & power

* **07:00 – 10:00** (configurable): screen on. Departures refresh every 30 s,
  traffic info every 3 min. The countdown ticks locally between refreshes, and
  only changed pixels are redrawn. CPU at 80 MHz, Wi-Fi in max modem-sleep,
  backlight dimmed to `BACKLIGHT_LEVEL`.
* **Rest of the day**: deep sleep with the panel in sleep mode, the backlight
  latched off and Wi-Fi off. The board wakes silently at most every 2 h to
  resync the clock over NTP, then switches on at 07:00.
* **Touch the screen while it is asleep** to show the board for 60 s.
* Tap the board to read the disruption messages. Tap again for the next one, or
  to go back. The board comes back on its own after 30 s.
* When first plugged in, it shows the board for 2 minutes so you can check
  everything works, then goes to sleep if it's outside the time window.

The ESP32 itself draws ~10 µA in deep sleep. The rest of the board (voltage
regulator, USB-serial chip, power LED if there is one) costs a few mA that
software can't remove. Expect roughly 100–150 mA with the screen on and
around 10–20 mA asleep. That's well under 1 € of electricity per year.

## Setup

1. **Credentials**: copy `include/secrets.example.h` to `include/secrets.h` and
   fill in your Wi-Fi and your PRIM API key
   (prim.iledefrance-mobilites.fr → *Mon compte* → *Mes jetons d'authentification*).
   `secrets.h` is git-ignored.
2. **Settings** (optional): `include/config.h`. You can change the time window,
   days (`ACTIVE_DAYS = 0x3E` for weekdays only), brightness, refresh rates,
   screen rotation and peek duration.
3. **Build & flash** with PlatformIO (already on this PC). All toolchains and
   libraries live in `.pio-core/` and `.pio/` inside this folder (`core_dir` in
   `platformio.ini`), so nothing is installed system-wide.

   ```bash
   pio run -t upload
   ```

   ```bash
   pio device monitor
   ```

   If the upload doesn't start, hold **BOOT** while plugging in or pressing **RST**.
   The serial monitor prints each API call (`[departures] ok, 5 trains`).

### If the colours look wrong

The ST7789 panels vary between batches. In `platformio.ini`:

* red and blue swapped (the RER A disc looks blue) → `TFT_RGB_ORDER=TFT_RGB`
* colours inverted (black background looks white) → `TFT_INVERSION_ON=1`
  instead of `TFT_INVERSION_OFF=1`
* image upside down → `SCREEN_ROTATION 3` in `config.h`

## How it works

| File | Role |
|---|---|
| `src/main.cpp` | Schedule, deep sleep, network task (core 0) / UI loop (core 1) |
| `src/prim_api.cpp` | PRIM calls, streaming JSON parsing with ArduinoJson filters |
| `src/ui.cpp` | LVGL screens (splash, board, traffic details) |
| `src/hw.cpp` | ST7789 + LVGL glue, backlight PWM, bit-banged XPT2046 touch, sleep pins |
| `src/text_utils.cpp` | HTML → text, accent folding, ISO-8601 parsing |
| `src/certs.h` | Google Trust Services roots for TLS verification of the PRIM server |
| `src/fonts/` | Generated fonts (Montserrat + FontAwesome icons, Latin-1) |

**APIs used** (header `apikey: <PRIM key>`):

* Next departures (SIRI Lite):
  `GET /marketplace/stop-monitoring?MonitoringRef=STIF:StopArea:SP:43135:&LineRef=STIF:Line::C01742:`
  Westbound trains are kept by excluding eastbound destinations
  (`EASTBOUND_DESTINATIONS` in `config.h`). All westbound RER A trains from
  Joinville stop at Auber.
* Traffic info (Navitia line reports):
  `GET /marketplace/v2/navitia/line_reports/lines/line%3AIDFM%3AC01742/line_reports`
  limited to today. Elevator messages are hidden unless `SHOW_ELEVATOR_OUTAGES` is set.

IDs come from the IDFM open data referential: stop area `43135` = Joinville-le-Pont
(railStation), line `C01742` = RER A.

## Tools (optional)

* `tools/preview/build_preview.py`: compiles the real UI code on Windows with the
  installed Visual Studio and renders sample screens to `.pio/preview/*.png`.
  Handy for tweaking the design without flashing.
* `tools/gen_fonts.sh`: regenerates `src/fonts/` (needs `lv_font_conv`,
  installed locally in `tools/fontgen`; see the script header).
* `tools/make_certs.py`: regenerates `src/certs.h` if the server's CA changes.
