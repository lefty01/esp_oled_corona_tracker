# covid-19 case tracker
get data from https://coronavirus-19-api.herokuapp.com and display on oled display

using esp8266/esp32 to connect securely (tls) to mqtt (mosquitto) server and publish the data as well


## config
make sure to change credentials in wifi_mqtt_creds.h


copy and paste your CA (which was used to gernerate client cert and private key) into server_mqtt.crt.h

copy client crt and key file to the respective client.crt.h and client.key.h file

## publish string
    mosquitto_pub -h host -p 8888 -u user -P pass --cafile myCA.pem \
		  --cert client.crt \
		  --key  client.key  \
		  -m "text" -t "/wemos/oled/set"

use logo as message to display logo again


# xbm image
you can use gimp to conver png to xbm format. load png file and resize to probably less then your oled display (here 128x64).
then export as "X BitMap image (*.xbm, *.icon, *.bitmap)". the "identifier prefix" is then used for _width, _height, and _bits name.
e.g. with identifier prefix 'mqtt' you get:

```
#define mqtt_width 53
#define mqtt_height 52
static unsigned char mqtt_bits[] = {
   0x80, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x03, 0x00, 0x00, 0x00,
   ...
```
