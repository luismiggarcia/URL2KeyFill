# Hardware connections and physical validation

## Distinct device models

| Device | Role in this application |
|---|---|
| UltraStudio **HD Mini** | Thunderbolt 3; two 3G-SDI outputs with external HD Fill/Key. Initial Full HD validation target. No physical UHD output. |
| Original UltraStudio **Mini Monitor** | A different, older product. Do not infer capabilities from the new 12G model; use only modes/pixels/keying confirmed by the SDK. |
| UltraStudio **Mini Monitor 12G** | Newer Thunderbolt 4 product; official specifications describe separate outputs, Fill/Key and UHD up to 2160p60. Not yet tested with this application. |

Product specifications do not replace `DoesSupportVideoMode` checks for the active profile, pixel format and exact frame rate. The application lists only formats implemented by its backend.

## Full HD setup — HD Mini

1. Install/authorize Desktop Video and restart if requested. Confirm that **Desktop Video Setup** recognizes the HD Mini. Use a compatible Thunderbolt 3 cable. Close Media Express, Resolve, OBS output or any other application that may occupy the output.
2. Connect **SDI OUT A → switcher Fill input** and **SDI OUT B → switcher Key input**, using independent 75-ohm connections. Do not connect them to SDI IN. The device keyer produces synchronized image and mask from one alpha-bearing video frame.
3. Configure an external linear/luma keyer, such as a downstream keyer, using those Fill and Key inputs. Start with non-inverted Key and no chroma key. Ensure that both inputs and the switcher use the same exact format/rate: 29.97 is not 30; 59.94 is not 60.
4. For 3G-SDI, configure Level A/B in Desktop Video Setup to match the receiver. Disable incompatible output conversions, PsF interpretation or dual-link image modes. The application does not change the user's persistent device profile or driver settings. Select SDI output in Desktop Video Setup where applicable.
5. If genlock is required, distribute suitable reference to the switcher and UltraStudio REF IN: tri-level or black burst as appropriate. Equivalent cable lengths help avoid different path delays. Verify reference lock on the hardware; the application does not measure external reference.
6. Open the application and use **SETUP → REFRESH**. Select HD Mini and a supported progressive 1920 × 1080 mode, initially 25 or 30 fps. Select **BROWSER SIZE → Match SDI output**, keep the test layer visible and press **PLAY**.
7. Observe A and B separately before keying the composition on air: A should show Fill, and B a grayscale mask. Confirm connectors on the actual unit. A local preview does not prove this behavior.

## Alpha, edges and levels

CEF supplies premultiplied alpha; the application delivers RGB with independent alpha to the SDK, following its KeyerOutput example. Start with an external linear key and determine the switcher's **premultiplied/pre-multiplied key** setting from the physical signals. SDK documentation alone does not prove how the Fill signal is treated after the hardware keyer.

Use a switcher background with a different color. Test transparent/opaque areas, 25/50/75% coverage, gradients, shadows and antialiased text. Dark edges may indicate double multiplication; bright edges may indicate missing multiplication. Do not destroy partial transparency by compensating with excessive clip/gain. Record the correct setting for the actual device/switcher combination.

Measure black, white and chroma on a Rec.709 waveform/vectorscope. Check legal levels and clipping. Transporting an RGB8 web source over a 10-bit SDI signal does not recover missing precision.

## Full HD acceptance protocol

Complete these checks for **each advertised mode intended for use**:

| Check | Evidence/criterion |
|---|---|
| Identification | Record model, ID, driver, profile, format, exact rational rate, pixel format and receiver. |
| Fill + Key | A and B simultaneously present, correctly routed and synchronized. |
| Geometry/orientation | Corners, circle, text and boundaries show no inversion or distortion. |
| Alpha | 0/25/50/75/100% coverage, shadows and antialiasing without halos. |
| Cadence | Animate for at least 10 minutes; record browser/SDI fps, late/dropped frames and external observations. Include 23.976/29.97/59.94 when advertised, as well as integer rates. |
| Static image | A static page retains continuous SDI even when CEF stops producing paints. |
| Pause/resume | PAUSE produces black Key after queued frames drain while SDI remains locked; resume restores image and audio. |
| Stop/restart | Repeat ten times without incorrect format or device-busy leaks. |
| Format change | Stop, choose another mode and start; verify the new cadence. |
| URL failure | Unreachable URL reports an error and clears that layer in subsequent frames. |
| Disconnect/reconnect | Disconnect Thunderbolt during a test; stop cleanly without a crash. Reconnect, Refresh and start PLAY manually. |
| Busy device | Another application occupies output; report a clear error while local configuration/preview remain usable. |
| Shutdown | Quit during playback; another application can subsequently acquire the output. |

SDK completion counters are not direct measurements at the physical connectors. Retain external observation/capture evidence too. A device without external keying may reproduce Fill and show a local mask, but cannot satisfy the simultaneous physical Fill/Key check.

## Stereo audio

The application embeds web audio in SDI at 48 kHz, delivering stereo 32-bit PCM to the SDK. At the receiver, verify channels 1/2, levels, distortion and A/V synchronization. Hidden layers are silent; PAUSE silences the composition while SDI continues. Already scheduled audio may drain for a few frames before silence takes effect.

Use a known audiovisual test page and a capture device or switcher with measurement capability. Scheduled-sample counters and non-zero PCM in the local mixer do not replace external listening or physical loopback.

## Future UHD validation

Once compatible hardware is available, such as the new Mini Monitor 12G, install a compatible driver and verify Thunderbolt requirements, 12G cabling and UHD receiver support. Repeat the acceptance protocol with an SDK-advertised **3840 × 2160 Keying mode**. Measure progressively toward the desired rate; this CPU implementation does not promise UHD60.

Rendering UHD and scaling to Full HD on the HD Mini tests scaling and HD output only. It is not physical UHD validation. Confirm BGRA8/ARGB8 Keying support on future devices; other formats require implementation.

## Official references consulted on 2026-10-28

- [UltraStudio/HD Mini specifications](https://www.blackmagicdesign.com/products/ultrastudio/techspecs)
- [UltraStudio Mini Monitor 12G specifications](https://www.blackmagicdesign.com/products/ultrastudiomini/techspecs)
- [SDK and software](https://www.blackmagicdesign.com/developer/products/capture-and-playback/sdk-and-software)
- [SDK keying overview](https://sdk-doc.blackmagicdesign.com/decklink-sdk/HighLevel/keying.html)
- [IDeckLinkKeyer](https://sdk-doc.blackmagicdesign.com/decklink-sdk/DeckLinkAPI/interfaces/idecklinkkeyer.html)
- [Output and mode validation](https://sdk-doc.blackmagicdesign.com/decklink-sdk/DeckLinkAPI/interfaces/idecklinkoutput.html)
- [SDK buffer access](https://sdk-doc.blackmagicdesign.com/decklink-sdk/DeckLinkAPI/interfaces/idecklinkvideobuffer.html)
- Headers and KeyerOutput/SignalGenerator examples in the official SDK 16.0 copy supplied separately under `third_party/DeckLinkSDK`.
- Pinned CEF headers/wrapper and the [official macOS example](https://github.com/chromiumembedded/cef/blob/master/tests/cefsimple/cefsimple_mac.mm).
