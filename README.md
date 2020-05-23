# Home Assistant Sommer Pro+ / Base+ Garage Door Opener

![Home Assistant Sommer Pro+ / Base+ Garage Door Opener](files/repo_image.png#center)

This is my version of the excellent [garage_pi](https://github.com/azrael783/garage_pi) project by [Daniel](https://github.com/azrael783/) that uses a WeMos D1 mini instead of a Raspberry Pi Zero.

## Prerequisites ##

**Software**
* [Arduino IDE](https://www.arduino.cc/en/main/software) - I'm using v1.8.12
* Arduino JSON - v6.15.1
* PubSubClient - v2.7.0
* ESP8266WiFi

**Hardware**

* [Sommer Pro+ / Base+](https://www.sommer.eu/en-GB/pro-base.html)
* [Sommer Conex Adapter](https://www.sommer-shops.eu/de/conex.html)
* [Wemos D1 Mini](https://docs.wemos.cc/en/latest/d1/d1_mini.html)
* [2 Channel-Relay](https://www.amazon.co.uk/gp/product/B07GXC4FGP?ref=ppx_pt2_dt_b_prod_image)
* [A 24V to 12v USB Power Adaptor](https://www.amazon.co.uk/gp/product/B07NMPDDN7/ref=ppx_yo_dt_b_asin_title_o02_s00?ie=UTF8&psc=1)

## Setup ##

**Flashing project to the WeMos**

Once you have setup your software, you first need to download this project, by clicking `clone -> Download ZIP`.

Unzip the file and navigate to the contents. you should see a file named `My_Helper_sample.h`, go ahead and rename it to `My_Helper.h`.

Now open `garage_door_sensor.ino`. Doing so should open it in the [Arduino IDE](https://www.arduino.cc/en/main/software).

You do not need to make any changes to the `garage_door_sensor` script itself!

Click on the `My_Helper.h` tab, that is where you will add your WiFi credentials for your home network & your MQTT details.

**Wiring**

The `5V` and `G` of the Wemos will connect to the `VCC` and `GND` of the relay. Then connect `D6` to `IN2` and `D5` to `IN1`.

Then you connect the relay to the Sommer Conex PCB. The `NO1` terminal should connect to `T1` on the Conex, `NO2` to `T2`, with both `COM` terminals connecting to the centre pin.

Finally the USB power Adapter needs to be plugged into the `24v` and `Ground` of the Sommer circuit board.

![Home Assistant Sommer Conex wiring diagram](files/garage_door_sensor_fritz.png)

## Upload ##

The next step, once you're happy with the details is to upload the project to the Wemos D1 Mini.

To do that you select `Tools` from the top menu within the IDE.

Then you need to set the upload parameters to the following;

![Arduino IDE Upload settings](files/upload_settings.png)

The next step is to simply plug your device in the computer, Go to `Tools -> Port` and select it.

For me this usually says something `dev/usbserial...`

Now you just click on the upload button in the top left corner of the IDE, it's the right-facing arrow.

After a bit of time compiling, the script should be uploaded to the device and begin scanning for the car.

You can see what the device is doing by going to `Tools -> Serial Monitor`

If the upload doesn't work the most likely cause is that you forgot to rename the `My_Helper_sample.h` file or import the above libraries.

## Integrating with Home Assistant ##

The simplest way to integrate with [Home Assistant](https://home-assistant.io) is to add a new `cover` to the `configuration.yaml` like this:

    cover:
    - platform: mqtt
      name: Garage Door
      command_topic: "garage/door/output"
      state_topic: "garage/door"
      state_open: "open"
      state_opening: "opening"
      state_closed: "closed"
      state_closing: "closing"
      payload_open: "OPEN"
      payload_close: "CLOSE"
      qos: 1
      optimistic: true
      device_class: garage
