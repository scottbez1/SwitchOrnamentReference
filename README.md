Reference code for my homemade Nintendo Switch ornament:

<a href="https://www.youtube.com/watch?v=zJxyTgLjIB8"><img src="https://img.youtube.com/vi/zJxyTgLjIB8/mqdefault.jpg" /></a>

Plays animated gifs from the SD card using the `bitbank2/AnimatedGIF` library and `TFT_eSPI` display driver.

Wifi and other settings (time zone, debug log visibility) are configured via a `config.json` file at the root of the SD card. Firmware can be updated by putting a `firmware.bin` file at the root of the SD card, or over wifi by entering the credits screen (click the right button) which enables ArduinoOTA.
