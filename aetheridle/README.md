# aetheridle

This is sway's idle management daemon, aetheridle. It is compatible with any
Wayland compositor which implements the
[ext-idle-notify](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/tree/main/staging/ext-idle-notify)
protocol. See the man page, [aetheridle(1)](./aetheridle.1.scd), for instructions
on configuring aetheridle.

## Release Signatures

Releases are signed with [34FF9526](https://keys.openpgp.org/search?q=34FF9526CFEF0E97A340E2E40FDE7BE0E88F5E48)
and published [on GitHub](https://github.com/swaywm/aetheridle/releases). aetheridle
releases are managed independently of sway releases.

## Installation

### From Packages

aetheridle is available in many distributions. Try installing the "aetheridle"
package for yours.

### Compiling from Source

Install dependencies:

* meson \*
* wayland
* wayland-protocols \*
* [scdoc](https://git.sr.ht/~sircmpwn/scdoc) (optional: man pages) \*
* git \*

_\* Compile-time dependency_

Run these commands:

    meson build/
    ninja -C build/
    sudo ninja -C build/ install
