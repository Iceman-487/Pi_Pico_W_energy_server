# Pi_Pico_W_energy_server
Arduino Wi-Fi Server - Modbus client

-How to Use Arduino IDE with Raspberry Pi Pico W: https://www.youtube.com/watch?v=yatxW3tMhRg&t=1235s
-Project created starting from example project "Hello Server"
-Energy meter model: Carlo Gavazzi EM511
-Since the WiFi.reconnect(); function doesn't work on this board, I had to "hard reset" the MCU each time the wifi connection is lost.
-To do the hard reset, pins 29 & 30 must be shorted together.
