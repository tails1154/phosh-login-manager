# Phosh GTK PIN Greeter

A small GTK4 PIN greeter for greetd, intended for a Phosh mobile session.

## Build

```sh
cc -O2 -Wall -Wextra phosh-greeter.c -o phosh-greeter-gtk \
  $(pkg-config --cflags --libs gtk4)
```

## Files

- `phosh-greeter.c` — GTK4 greeter using greetd IPC
- `phosh-greeter.conf` — fixed username configuration
- `phosh-session-wrapper` — Phosh startup environment and DRM handoff
- `phosh-environment.conf` — user environment for Phosh
- `greetd-config.toml` — greetd Cage configuration
- `portals.conf` — preferred desktop portal configuration

The session wrapper supports `--test`, which runs a 20-second headless
Phoc/Phosh smoke test without claiming the real DRM device.

This is system-specific configuration; review paths, username, and PAM/greetd
setup before installing it on another machine.
