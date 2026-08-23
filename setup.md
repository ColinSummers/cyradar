# CYRadar Setup

## What's in the box

A small screen that shows live aircraft traffic around your local airport, using data from the OpenSky Network. It needs power (USB-C) and a WiFi connection to get started.

## Step 1: Power it on

Plug in the USB-C cable. The screen will show "CYRadar" followed by "Connecting to WiFi..." and then a setup screen.

## Step 2: Connect it to your WiFi

Since the radar doesn't have a keyboard, you use your phone or laptop to tell it your WiFi password.

1. The screen will show **"Connect to WiFi: CYRadarWiFi"** and the address **192.168.4.1**.
2. On your phone or laptop, open your WiFi settings and join the network called **CYRadarWiFi**.
3. A setup page should pop up automatically. If it doesn't, open a browser and go to **192.168.4.1**.
4. Tap **Configure WiFi**.
5. Choose your home WiFi network from the list and enter the password.
6. Tap **Save**.

The radar will restart, connect to your WiFi, and briefly show its IP address (something like 192.168.1.42). Write this down -- you'll need it if you want to change settings later.

If it can't connect (wrong password, etc.), it will go back to showing the setup screen so you can try again.

## Step 3: You're done

After connecting to WiFi, the radar starts working immediately. It fetches nearby aircraft and local weather data, and the green sweep hand begins rotating. Your airport has been pre-configured, so everything should be showing your local traffic right away.

## Using the radar

### The main screen

The circular radar scope on the left shows aircraft within range as small green triangles pointing the direction they're flying. The green sweep hand rotates once per data refresh (roughly every 20 seconds). Your airport runway is drawn as a blue line through the center.

The sidebar on the right shows:
- Your airport identifier and the current scan diameter in nautical miles
- How many aircraft are currently airborne in range
- A scrolling list of aircraft callsigns
- The device's IP address at the bottom

### Touch controls

The radar screen is divided into four zones you can tap:

| Zone | What it does |
|------|-------------|
| **Top-left** | Zoom in (smaller area, more detail) |
| **Bottom-left** | Zoom out (larger area, wider view) |
| **Top-right** | Show detailed weather (METAR) |
| **Bottom-right** | Show weather map |

From the weather screens, tap anywhere to return to the radar. They also return automatically after about 15 seconds.

### Weather screens

- **METAR Detail** -- Wind, visibility, ceiling, temperature, altimeter, crosswind analysis for each runway, and the raw observation text.
- **Weather Map** -- A geographic plot of nearby stations, color-coded by flight category (green = VFR, blue = MVFR, pink = IFR, red = LIFR).
- **TAF Grid** -- From the weather map, tap the bottom-right corner to see TAF forecasts as a grid of colored dots showing how conditions are expected to change.

### Known tails

If you've added tail numbers in the configuration (see below), those aircraft appear in **yellow** instead of green, so they stand out. Their display name (if you set one) appears in the sidebar list.

## Configuration

You can change settings from any device on the same WiFi network.

1. Open a browser and go to the radar's IP address (shown at the bottom of the radar screen). For example: **http://192.168.1.42**

   You can also try **http://cyradar.local** -- this works on most phones and laptops without needing to know the IP address.

2. The configuration page will appear. Change whatever you need and tap **Save**. The radar will restart with your new settings.

While the configuration page is open, the radar screen shows "Configuration in flux..." -- this is normal.

### Configuration fields

**Airport**
- **Airport** -- Your 4-letter ICAO airport identifier (e.g., KFHR, KBFI, KSEA). After entering it, tap **Lookup** to automatically fill in the latitude, longitude, and runway data.
- **Latitude / Longitude** -- Filled in automatically by Lookup. You can fine-tune these if you want the radar centered on your house instead of the airport.
- **Ignore above (ft MSL)** -- Aircraft above this altitude are filtered out. The default (8,000 ft) hides high-altitude overflights so the display stays focused on local traffic. Raise it if you want to see everything.

**Radar Display**
- **Diameter** -- How wide the radar scope is, in nautical miles. Smaller numbers show more detail; larger numbers show a wider area. You can also change this with the zoom touch controls.
- **Sweep** -- Toggles the rotating green sweep hand on or off.
- **Info text** -- Toggles the callsign and altitude labels next to each aircraft.

**Weather Stations**
- **METAR map stations** -- Space-separated list of ICAO airport IDs to show on the weather map. These are the stations whose current conditions are displayed.
- **TAF grid stations** -- Space-separated list of up to 8 stations for the TAF forecast grid. The first 4 are the top row, the next 4 are the bottom row.

**OpenSky**
- **Client ID / Client Secret** -- Credentials for the OpenSky Network API. These are pre-configured. With credentials, the radar refreshes about every 20 seconds; without them, about every 3.5 minutes.

**Known Tails**
- A space-separated list of aircraft tail numbers you want to highlight. Add a name in parentheses to give them a label:

  ```
  N12345 N67890(Dad) N11111(Flight School)
  ```

  Known aircraft appear in yellow on the radar and in the sidebar.

## Firmware updates

The radar checks for firmware updates automatically once a day. You can also trigger a manual check:

1. Tap the **top-right** of the radar to open the METAR detail screen.
2. Tap the **FW Update** button in the bottom-right corner.
3. If an update is available, the screen will show "Updating firmware..." and the radar will restart when it's done.

## Changing WiFi networks

If you move the radar to a new location or change your WiFi password, it won't be able to connect and will automatically go back to the setup screen from Step 2. Join the **CYRadarWiFi** network from your phone and enter the new WiFi credentials.

## Troubleshooting

**The screen is frozen but the device is still on** -- Unplug and replug the USB-C cable. The radar will restart and reconnect.

**No aircraft are showing up** -- Check that the airport identifier is correct in the configuration. Also make sure the scan diameter isn't set too small -- try zooming out (tap bottom-left a few times).

**There's no sweep hand** -- The sweep animation can be toggled off in the configuration. Check that "Sweep" is enabled.

**Can't reach the configuration page** -- Make sure your phone/laptop is on the same WiFi network as the radar. Try the IP address shown at the bottom of the radar screen.
