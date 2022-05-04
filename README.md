# Twitterframe
For creating a digital "picture frame" that displays the latest tweets and attached photos from a selected user using an Arduino and TFT LCD touchscreen.

More info to come

# Materials 
[Arduino MKR1000 WiFi](https://store-usa.arduino.cc/products/arduino-mkr1000-wifi-with-headers-mounted)

[Screen - HiLetgo ILI9341 2.8" SPI TFT LCD Display Touch Panel 240X320](https://www.amazon.com/dp/B073R7BH1B?psc=1&ref=ppx_yo2ov_dt_b_product_details)

The screen did not come with headers for the SD card pins so I soldered some on

<img width="300" alt="Screen Shot 2022-05-03 at 11 51 12 PM" src="https://user-images.githubusercontent.com/23437904/166634531-89c8f8dd-41c9-4027-87d3-45945a5d57e3.png"><img width="300" alt="Screen Shot 2022-05-03 at 11 51 25 PM" src="https://user-images.githubusercontent.com/23437904/166634538-ee7ec2f1-46c3-4fd5-a1de-78b2f8b81b5f.png">


# Demo video 
https://youtu.be/g-cVE6Adopg


# Wiring Diagram
<img width="1309" alt="Screen Shot 2022-05-03 at 11 40 45 PM" src="https://user-images.githubusercontent.com/23437904/166633398-1df83693-dc16-442f-932a-1cbb1bf089da.png">

# Notes / References
JPEG compression/decompression, scaling, and push to screen adapted from [here](https://forum.arduino.cc/t/jpeg-picture-on-adafruit-3-5-320x480-color-tft-touchscreen/561028/8). I made some adjustments to allow for custom placement of the image on the screen instead of only in the corner.

[Twitter API](https://developer.twitter.com/en/docs/twitter-api) and [Generating a Bearer token](https://developer.twitter.com/en/docs/authentication/oauth-2-0/bearer-tokens)
