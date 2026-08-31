# CYRadar Setup

Note: This is a **toy.** This is not something you should use for making aeronautical decisions. That should be obvious to any experienced pilot, but just in case: it is a toy, possibly with bad data which is poorly or incorrectly presented. I like playing with toys.

## What's in the Box

A small screen that shows live aircraft traffic around your local airport, using data from the OpenSky Network. It needs power via a usb-c cable, and it requires WiFi to work. (Ideally you should have a (free) OpenSky account (https://opensky-network.org). If you don't, the data is refreshed every 3 minutes or so, but with the account it drops to refreshing every 20 seconds.)


## Step 1: Power it on

Plug in the usb-c cable. It is standard, so anyone you have that works is fine. If you need a super long one, just order one online. If you want to bring it to a coffee shop with you one of those 'extend your phone's battery' devices with a usb-c cable will be fine.

Once you provide power the screen will show "CYRadar" followed by "Connecting to WiFi..." Since it does not know your WiFi password it will bring up the set up screen.


## Step 2: Connect it to your WiFi

Since the radar doesn't have a keyboard, you use your phone or laptop to tell it your WiFi password. The little board will helpfully start its *own* WiFi network for you to connect to and give it the password to *your* WiFi network:

1. The setup screen will show **"Connect to WiFi: CYRadarWiFi"** and the address **192.168.4.1**.
2. On your phone or laptop, open your WiFi settings and join the network called **CYRadarWiFi**.
3. A setup page should pop up automatically. If it doesn't, open a browser (like Safari or Chrome) and go to **192.168.4.1**.
4. Tap **Configure WiFi**.
5. Choose your home WiFi network from the list and enter the password.
6. Tap **Save**.

The radar will restart, connect to your WiFi, and briefly show its IP address (something like 192.168.1.42). Write this down -- you'll need it if you want to change settings later. (If you fail to write it down don't worry, you can find it on the screen later.)

If it can't connect (wrong password, etc.), it will go back to showing the setup screen so you can try again.


## Step 3: You're done

After connecting to WiFi, the radar starts working immediately. It fetches nearby aircraft and local weather data, and the green sweep hand begins rotating. Your airport has been pre-configured, so everything should be showing your local traffic right away.


## Using the radar

### The Main Screen

The circular radar scope on the left shows aircraft within range as small green triangles pointing the direction they're flying. The green sweep hand rotates once per data refresh (roughly every 20 seconds). Your airport runway is drawn as a blue line through the center.

The sidebar on the right shows:
- Your airport identifier and the current outer scan diameter in nautical miles
- How many aircraft are currently airborne in range
- A scrolling list of aircraft callsigns
- The device's IP address at the bottom

### Touch controls

The radar screen is divided into four zones you can tap:

| Zone | What it does |
|------|-------------|
| **Top-left** | Zoom in (smaller area, more detail) |
| **Bottom-left** | Zoom out (larger area, wider view) |
| **Top-right** | Show detailed weather at your airport (METAR) |
| **Bottom-right** | Show weather map, tap again, page of TAFs |

From the weather screens, tap anywhere to return to the radar. They will return automatically after about 15 seconds.

### Weather screens

- **METAR Detail** -- Wind, visibility, ceiling, temperature, altimeter, crosswind analysis for each runway, and the raw observation text.
- **Weather Map** -- A geographic plot of nearby stations, color-coded by flight category (green = VFR, blue = MVFR, pink = IFR, red = LIFR).
- **TAF Grid** -- From the weather map, tap the bottom-right corner to see TAF forecasts as a grid of colored dots showing how conditions are expected to change.

### Known tails

If you've added tail numbers in the configuration, those aircraft appear in **yellow** instead of green, so they stand out. You can choose to display a name instead of the tail number.

## Configuration

You can change settings from any device on the same WiFi network.

1. Open a browser and go to the radar's IP address (shown at the bottom of the radar screen). For example: **http://192.168.1.42**

   You can also try **http://cyradar.local** -- this works on most phones and laptops without needing to know or type the IP address.

2. The configuration page will appear. Change whatever you need and tap **Save**. The radar will restart with your new settings.

While the configuration page is open, the radar screen shows "Configuration in flux..." -- this is normal; its brain is tiny and it can't do two things at once.

### Configuration fields

**Airport**
- **Airport** -- Your 4-letter ICAO airport identifier (e.g., KFHR, KBFI, KSEA). After entering it, tap **Lookup** to automatically fill in the latitude, longitude, and runway data.
- **Latitude / Longitude** -- Filled in automatically by Lookup. You can fine-tune these if you want the radar centered on another point instead of the airport.
- **Ignore above (ft MSL)** -- Aircraft above this altitude are filtered out. The default (8,000 ft) hides high-altitude overflights so the display stays focused on local traffic. Raise it if you want to see more.

**Radar Display**
- **Diameter** -- How wide the radar scope is, in nautical miles. Smaller numbers show more detail; larger numbers show a wider area. You can also change this with the zoom touch controls.
- **Sweep** -- Toggles the rotating green sweep hand on or off.
- **Info text** -- Toggles the callsign and altitude labels next to each aircraft.

**Weather Stations**
- **METAR map stations** -- Space-separated list of ICAO airport IDs to show on the weather map. These are the stations whose current conditions are displayed. Obviously, this is the page you glance at to see how your general area is faring.
- **TAF grid stations** -- Space-separated list of up to 8 stations for the TAF forecast grid. The first 4 are the top row, the next 4 are the bottom row. This will show on the second page of area information in a display that *should* give you some sense of the area over the next 16 hours or so.

**OpenSky**
- **Client ID / Client Secret** -- Credentials for the OpenSky Network API. With credentials, the radar refreshes about every 20 seconds; without them, about every 3.5 minutes.

**Known Tails**
- A space-separated list of aircraft tail numbers you want to highlight. Add a name in parentheses to replace the tail number on the display:

  ```
  N12345 N67890 (FBI) N321BS (Flight School) N-X-211 (Lindy)
  ```

  Known aircraft appear in yellow on the radar and in the sidebar.





## Firmware updates

The radar checks for firmware updates automatically once a day or when it first boots up. You can also trigger a manual check:

1. Tap the **top-right** of the radar to open the METAR detail screen.
2. Tap the **FW Update** button in the bottom-right corner.
3. If an update is available, the screen will show "Updating firmware..." and the radar will restart when it's done.

## Changing WiFi networks

If you move the radar to a new location or change your WiFi password, it won't be able to connect and will automatically go back to the setup screen from Step 2. Join the **CYRadarWiFi** network from your phone and enter the new WiFi credentials.

## Troubleshooting

**The screen is frozen but the device is still on** -- Unplug and replug the USB-C cable. The radar will restart and reconnect.

**No aircraft are showing up** -- Check that the airport identifier is correct in the configuration. Also make sure the scan diameter isn't set too small -- try zooming out (tap bottom-left a few times).

**There's no sweep hand** -- The sweep animation can be toggled off in the configuration. Check that "Sweep" is enabled.

**Can't reach the configuration page** -- Make sure your phone/laptop is on the same WiFi network as the radar. Try the IP address shown at the bottom of the radar screen. Also try adding :8080 to the end of what you are putting in your browser. Some academic or office WiFi networks do not allow 'web serving' on port 80 from any device *on* the network. (This is a wise choice, I could pretend to be Amazon otherwise.)
