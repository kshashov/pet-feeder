# Pet Feeder

![img.png](img.png | width=500)

## Software
* Arduino script
* Host WiFi network `pet-feeder/12345678`  with index page. Restart after proper `ssid/password` are set
* Connect to WIFI and get correct time from ntp server
* Turn Led 
  * White - OK
  * Blue - Feeding
  * Green - Host WIFI  
  * Red - Could not get time
* Provide `/alarms` endpoint
  * GET `alarms`
    ```json
    [
        {"time":23400,"alarmed":true,"size":3},
        {"time":57600,"alarmed":true,"size":2},
        {"time":72000,"alarmed":false,"size":2},
        {"time":84600,"alarmed":false,"size":3}
    ]
    ```
  * PUT `/alarms`
    ```json
    {"time":23400,"alarmed":true,"size":3}
    ```
  * PATCH `/alarms` supports only size change
    ```json
    {"time":23400,"size":3}
    ```
  * DELETE `/alarms?time=23400` 
  * POST `/alarms/skip?time=23400` set `alarmed=true`
  * POST `/alarms/feed`
    * `/alarms/feed?time=23400` just run feeding
    * `/alarms/feed?size=2` run with size from existing alarm and set `alarmed=true`
* Run feeding if physical button is pressed 

## Client app
* Made on Flutter
* Features
  * Feed now
  * Add/Edit/Delete feedings 
![img_1.png](img_1.png | width=500)

## Hardware
* Esp32
* RGB Led
* Button
* Nema17 + a4988 driver
* MP1584EN dc dc buck converter + 5.5x2.1mm Female DC Power Plug

![img_2.png](img_2.png | width=500)
![img_3.png](img_3.png | width=500)
![img_4.png](img_4.png | width=500)