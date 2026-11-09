# asus-hotkeys and ROG Licht

Two small programs for the ASUS ROG Strix SCAR 18 G835LXG (details: [docs/asus-hotkeys.md](../../docs/asus-hotkeys.md)):

- **asus-hotkeys** – background agent (LaunchAgent). Handles the Fn keys and is the only process
  that talks to the N-KEY keyboard (needs **Input Monitoring**). Fn+F7/F8 set the real panel
  backlight when NullMoth provides one (boot-arg `nvrmacpibl=1`), otherwise gamma dimming.
- **ROG Licht** – menu bar app, the macOS stand-in for Armoury Crate's lighting page:
  performance profile (Quiet / Balanced / Turbo), Aura effect, colour(s), speed, direction,
  brightness, and the lighting zones **keyboard, light bar, logo**, each separately for
  awake / boot / sleep / shutdown. It writes `lighting.json` and asks the agent (distributed
  notification `de.codex.asushotkeys.lighting`) to apply it, so the app itself needs no permission.

```bash
./install.sh
```

Then allow **asus-hotkeys** in System Settings → Privacy & Security → Input Monitoring.

Files in `~/Library/Application Support/ASUSHotkeys/`:
- `lighting.json` – lighting state (shared by app and agent)
- `config.json` – key mapping (actions `profile`, `kbd+`, `kbd-`, `dim+`, `dim-`, `aura`)
