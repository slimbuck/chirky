# SNES controller to USB with a Raspberry Pi Pico

This is the known-good configuration for connecting one original-style SNES
controller directly to a Raspberry Pi Pico and using it as Chirky's USB
controller. It uses GP2040-CE 0.7.12 and presents the controller as Generic HID.

## Exact hardware

The tested setup uses:

- One original Raspberry Pi Pico with the RP2040 microcontroller and Micro-USB
  connector. This is the standard non-W, non-Pico-2 board. A Pico H is
  electrically equivalent for this setup because it is the same board with
  headers already fitted.
- One wired SNES-compatible controller or female SNES extension lead, connected
  using the five power, ground, clock, latch, and data wires listed below.
- One Micro-USB cable that supports data, not a charge-only cable.
- A Raspberry Pi/Linux host, or a Windows laptop for configuration and testing.

The working build is wired directly to the Pico headers. The Pico-to-Pi HAT
adapter is not used in this final configuration. Pico W, Pico 2, Pico 2 W, and
other RP2040/RP2350 boards may require a different GP2040-CE build and are not
covered by this tested recipe.

## Required downloads

Download these exact files from the official GP2040-CE v0.7.12 release:

| File | Purpose | Size | SHA-256 |
| --- | --- | ---: | --- |
| [GP2040-CE_0.7.12_Pico.uf2](https://github.com/OpenStickCommunity/GP2040-CE/releases/download/v0.7.12/GP2040-CE_0.7.12_Pico.uf2) | Main firmware for a standard RP2040 Raspberry Pi Pico | 2,426,880 bytes | `bbbb3dab7e5d1d1ccdb37de12b74e974862381b71e3d3c9a60cc574b9f93d8fe` |
| [force_webconfig.uf2](https://github.com/OpenStickCommunity/GP2040-CE/releases/download/v0.7.12/force_webconfig.uf2) | One-shot boot into the local web configurator | 22,528 bytes | `334d0869e19a7da9c8cc589da6927beb00907bad2e70b16a4192db4882b79208` |

`flash_nuke.uf2` is not needed for a normal reinstall. Keep it as a recovery
file if the Pico has run unrelated firmware or its saved configuration is
corrupt:

| File | Purpose | Size | SHA-256 |
| --- | --- | ---: | --- |
| [flash_nuke.uf2](https://github.com/OpenStickCommunity/GP2040-CE/releases/download/v0.7.12/flash_nuke.uf2) | Erases firmware configuration before a clean reinstall | 98,304 bytes | `c791c317a43552769089f133b1cb838f36139afdc4fc6499b5773ba78103d255` |

Verify a download in PowerShell with:

```powershell
Get-FileHash -Algorithm SHA256 .\GP2040-CE_0.7.12_Pico.uf2
Get-FileHash -Algorithm SHA256 .\force_webconfig.uf2
```

Use the `Pico` build. Do not use `Blank`, `PicoW`, `Pico2`, or a build named
for another controller board.

## Wiring

The tested wiring is:

| SNES pin | Signal | Pico GPIO | Pico physical pin |
| ---: | --- | --- | ---: |
| 1 | Power | `3V3 OUT` | 36 |
| 2 | Clock | `GP3` | 5 |
| 3 | Latch | `GP2` | 4 |
| 4 | Data | `GP4` | 6 |
| 5 | Not connected | - | - |
| 6 | Not connected | - | - |
| 7 | Ground | `GND` | Any GND, including 38 |

The installed cable uses green for power and brown for ground. Do not infer the
other signals from colour: extension and third-party cable colours are not
standard. Confirm each connector contact with a continuity meter.

Use `3V3 OUT`, not `3V3_EN`. `3V3_EN` controls the Pico's voltage regulator and
must remain disconnected. Do not power the pad from 5 V without suitable level
conversion because RP2040 GPIO is not 5 V tolerant.

The critical signal order is:

```text
Clock -> GP3
Latch -> GP2
Data  -> GP4
```

## Install the firmware

1. Unplug the Pico from USB.
2. Hold the Pico's `BOOTSEL` button while connecting it to the laptop.
3. Release `BOOTSEL` when the `RPI-RP2` removable drive appears.
4. Copy `GP2040-CE_0.7.12_Pico.uf2` to `RPI-RP2`.
5. Wait for the drive to disappear and the Pico to reboot.

If a completely clean install is required, copy `flash_nuke.uf2` first. Wait
for `RPI-RP2` to reappear, then copy the main Pico firmware. Flash nuking erases
all saved pin and add-on configuration.

## Open the web configurator

The configurator is served locally by the Pico; it is not an internet site.

1. Re-enter BOOTSEL mode so `RPI-RP2` appears.
2. Copy `force_webconfig.uf2` to the drive.
3. Wait for the Pico to reconnect as a USB network device.
4. Open [http://192.168.7.1](http://192.168.7.1).

Use `http`, not `https`. `force_webconfig.uf2` is a one-shot command and does
not replace the main firmware or erase its settings. Copy it again whenever
web-config is needed and the controller's Start input is not available. Once
the SNES input works, holding Start while connecting USB can enter web-config.

## GP2040-CE settings

In **Configuration > Add-ons Configuration > SNES Input** (called **SNES
Extension** in some layouts), set:

```text
Enabled: Yes
Clock Pin: 3
Latch Pin: 2
Data Pin: 4
```

Click the page's **Save** button. In **GPIO Pin Mapping**, `GP2`, `GP3`, and
`GP4` should then be unavailable or labelled **Assigned to Add-on**. They must
not remain ordinary Up, Down, or Right button inputs.

In **Settings**, set:

```text
Input Mode: Generic HID
```

Save again, then choose **Reboot > Controller**. Generic HID is the tested mode
for Chirky on Linux and retains GP2040-CE's 1 ms USB polling interval.

## Test and use with Chirky

On Windows, run `joy.cpl`, select the GP2040-CE Generic controller, open
**Properties > Test**, and check every direction and button. Inputs must return
to neutral immediately after release.

On Chirky, open **Settings > Input Settings > Map controller** and follow
the prompts for all eight Chirky inputs. The Pico firmware translates the serial
SNES protocol into a standard USB controller; Chirky then maps that USB device
to its logical Chirky inputs (B to Primary, Y to Secondary, Start to Start, and Select to Menu).

## Troubleshooting

- `RPI-RP2` remains visible: the Pico is in BOOTSEL mode, not controller mode.
  Unplug it and reconnect without holding `BOOTSEL`.
- The configurator does not load: use `http://192.168.7.1`, not HTTPS. Windows
  should show a `Remote NDIS based Internet Sharing Device` with address
  `192.168.7.2`.
- Only B appears as POV-hat Right: SNES Input is disabled and `GP4` is still
  using the Pico build's default Right mapping.
- Buttons appear stuck or several inputs fire incorrectly: Clock and Latch are
  probably reversed. The working configuration is Clock `GP3`, Latch `GP2`.
- Windows identifies an Xbox 360 controller: USB is working, but Input Mode is
  XInput. Select Generic HID for the known Chirky configuration.
- The Pico works as USB but no SNES input appears: verify that SNES Input is
  enabled, its pins are 3/2/4, and GPIO Pin Mapping shows those pins assigned
  to the add-on.

## Automation

Most of the repeat setup can be automated, but a stock Pico still needs a
physical way to enter BOOTSEL for the initial flash or recovery.

The least fragile repeatable process is:

1. Configure one Pico through web-config and verify every button.
2. Use GP2040-CE's **Data Backup and Restoration** page to export its settings.
3. Keep that versioned backup with the matching GP2040-CE release.
4. Flash another Pico with the same firmware, enter web-config, and import the
   backup instead of entering every setting manually.

A Windows PowerShell helper can automate downloading the three pinned assets,
checking their SHA-256 hashes, waiting for the `RPI-RP2` drive, and copying the
selected UF2. A second helper can wait for `192.168.7.1`, verify the firmware
version, apply the settings through GP2040-CE's local HTTP API, read them back,
and reboot into controller mode. That HTTP API is internal rather than a stable
public contract, so such a script should be pinned to GP2040-CE 0.7.12 and must
verify every value after writing it.

Full hands-off flashing would additionally require hardware control of the
Pico's `RUN` and `BOOTSEL` signals, or compatible bootloader tooling and a
firmware-assisted reboot path. For one or two controllers, physical BOOTSEL plus
an imported configuration backup is simpler and easier to recover.

## References

- [GP2040-CE v0.7.12 release](https://github.com/OpenStickCommunity/GP2040-CE/releases/tag/v0.7.12)
- [GP2040-CE firmware installation](https://gp2040-ce.info/installation/)
- [GP2040-CE SNES Input add-on](https://gp2040-ce.info/add-ons/snes-input/)
- [GP2040-CE web configurator](https://gp2040-ce.info/web-configurator/)
