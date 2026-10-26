# SwitchWebFiles

A homebrew HTTP file manager for Nintendo Switch.

## Features

- Browser-based interface
- SD card directory listing
- File downloads
- File uploads from Safari
- Local-network operation
- No iPhone app required

## Requirements

- Nintendo Switch running Atmosphere
- Homebrew access
- Wi-Fi connection
- An iPhone or another device with a modern browser

## Build

The project uses devkitPro, devkitA64, and libnx.

Run:

    make

The output should be:

    SwitchWebFiles.nro

GitHub Actions builds the application automatically when you push
to the main branch or manually start the workflow.

## Install

Copy SwitchWebFiles.nro to:

    /switch/SwitchWebFiles.nro

Launch it through the Homebrew Menu.

## Connect

The server listens on port 8080.

Find the Switch's local IP address in its network settings.

On your iPhone, open:

    http://SWITCH-IP:8080/

Replace SWITCH-IP with the Switch's actual local IP address.

## Upload limits

The initial implementation limits individual uploads to 16 MiB.

Files with an existing destination name are not overwritten.

## Security

This application is intended for a trusted local network.

Do not expose the server to the public internet.

The initial implementation is experimental. Review and test it
before using it with important files.

## Current limitations

- No authentication
- No HTTPS
- No recursive folder creation
- No delete interface
- No overwrite support
- Limited handling of unusual filenames
- The server handles requests sequentially

Keep the Switch awake during transfers.
