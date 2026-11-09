# clamshell-agent – lid closed with an external display

External displays on the HDMI port work with NullMoth 2.8.1 **when they are connected at boot**
(a display plugged in later is not detected yet: NVRMFB lights the head, but logs
`fb1 connect notify: nothing registered yet`, so macOS never gets the display; logging out and in does not help). macOS notices the lid (`AppleClamshellState`), but it
only switches the built-in panel off for an `AppleBacklightDisplay`; NullMoth's framebuffer has no
backlight control, so the panel is a plain `AppleDisplay` and stays the main display when the lid closes.

`clamshell-agent` does the switch itself (checked every second):

| State | Action |
| --- | --- |
| lid closed + external display online | external display becomes main (menu bar, Dock), built-in panel off |
| lid open | built-in panel on and main again |
| lid closed, external display unplugged | built-in panel on immediately |

Changes are applied for the current session only, so a reboot or logout always brings the panel back.
Switching the panel off uses SkyLight's private `CGSConfigureDisplayEnabled` (as display tools do).

```bash
./install.sh             # build + LaunchAgent as.saeckl.clamshell (no admin rights)
./install.sh uninstall   # remove
tail ~/Library/Logs/clamshell-agent.log
```

Tested on the G835LXG with a Dell U2415 on HDMI (9 Nov 2026): lid closed → Dell main, panel off;
lid open → panel back after ~2 s.
