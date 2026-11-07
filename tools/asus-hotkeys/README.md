# asus-hotkeys

User agent for the ASUS Fn keys on the G835LXG (no kext needed except
[ASUSG835WMI](../../drivers/ASUSG835WMI) for Fn+M4). Details: [docs/asus-hotkeys.md](../../docs/asus-hotkeys.md).

```bash
./install.sh
```

Then allow **asus-hotkeys** in System Settings → Privacy & Security → Input Monitoring.
Key mapping and Aura effects: `~/Library/Application Support/ASUSHotkeys/config.json`
(actions `profile`, `kbd+`, `kbd-`, `dim+`, `dim-`, `aura`; re-read on every key press).
