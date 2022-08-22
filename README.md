# Impetus

[![Packaging status](https://repology.org/badge/tiny-repos/keyd.svg)](https://repology.org/project/keyd/versions)

Linux lacks a good key remapping solution. In order to achieve satisfactory
results a medley of tools need to be employed (e.g xcape, xmodmap) with the end
result often being tethered to a specified environment (X11). keyd attempts to
solve this problem by providing a flexible system wide daemon which remaps keys
using kernel level input primitives (evdev, uinput).

# UPDATE (v2.4.0 released)

The config format has undergone several iterations since the first
release, for those migrating their configs from v1, please see the
[changelog](docs/CHANGELOG.md)*.

# Goals

  - Speed       (a hand tuned input loop written in C that takes <<1ms)
  - Simplicity  (a [config format](#sample-config) that is intuitive)
  - Consistency (modifiers that [play nicely with layers](https://github.com/rvaiya/keyd/blob/6dc2d5c4ea76802fd192b143bdd53b1787fd6deb/docs/keyd.scdoc#L128) by default)
  - Modularity  (a UNIXy core extensible through the use of an [IPC](https://github.com/rvaiya/keyd/blob/90973686723522c2e44d8e90bb3508a6da625a20/docs/keyd.scdoc#L391) mechanism)

# Features

keyd has several unique features many of which are traditionally only
found in custom keyboard firmware like [QMK](https://github.com/qmk/qmk_firmware)
as well as some which are unique to keyd.

Some of the more interesting ones include:

- Layers (with support for [hybrid modifiers](https://github.com/rvaiya/keyd/blob/6dc2d5c4ea76802fd192b143bdd53b1787fd6deb/docs/keyd.scdoc#L128)).
- Key overloading (different behaviour on tap/hold).
- Keyboard specific configuration.
- Instantaneous remapping (no more flashing :)).
- A client-server model that facilitates scripting and display server agnostic application remapping. (Currently ships with support for X, sway, and gnome).
- System wide config (works in a VT).
- First class support for modifier overloading.
- Unicode support.

### keyd is for people who:

 - Would like to experiment with custom [layers](https://beta.docs.qmk.fm/using-qmk/software-features/feature_layers) (i.e custom shift keys)
   and oneshot modifiers.
 - Want to have multiple keyboards with different layouts on the same machine.
 - Want to be able to remap `C-1` without breaking modifier semantics.
 - Want a keyboard config format which is easy to grok.
 - Like tiny daemons that adhere to the Unix philosophy.
 - Want to put the control and escape keys where God intended.
 - Wish to be able to switch to a VT to debug something without breaking their keymap.

### What keyd isn't:

 - A tool for programming individual key up/down events.

# Dependencies

 - Your favourite C compiler
 - Linux kernel headers (already present on most systems)

## Optional

 - python      (for application specific remapping)
 - python-xlib (only for X support)

# Installation

*Note:* master serves as the development branch, things may occasionally break
between releases. Releases are [tagged](https://github.com/rvaiya/keyd/tags), and should be considered stable.

## From Source

    git clone https://github.com/rvaiya/keyd
    cd keyd
    make && sudo make install
    sudo systemctl enable keyd && sudo systemctl start keyd

# Quickstart

1. Install keyd

2. Put the following in `/etc/keyd/default.conf`:

```
[ids]

*

[main]

# Maps capslock to escape when pressed and control when held.
capslock = overload(control, esc)

# Remaps the escape key to capslock
esc = capslock
```

3. Run `sudo systemctl restart keyd` to reload the config file.

4. See the man page (`man keyd`) for a more comprehensive description.

5. Key names can be obtained with interactive monitoring `sudo keyd -m`

6. System service logs can be read with `sudo journalctl -e -u keyd`.

*Note*: It is possible to render your machine unusable with a bad config file.
Should you find yourself in this position, the special key sequence
`backspace+escape+enter` should cause keyd to terminate.

Some mice (e.g the Logitech MX Master) are capable of emitting keys and
are consequently matched by the wildcard id. It may be necessary to
explicitly blacklist these.

## Application Specific Remapping (experimental)

- Add yourself to the keyd group:

	`usermod -aG keyd <user>`

- Populate `~/.config/keyd/app.conf`:

E.G

	[alacritty]

	alt.] = macro(C-g n)
	alt.[ = macro(C-g p)

	[chromium]

	alt.[ = C-S-tab
	alt.] = macro(C-tab)

- Run:

	`keyd-application-mapper`

You will probably want to put `keyd-application-mapper -d` somewhere in your 
display server initialization logic (e.g ~/.xinitrc) unless you are running Gnome.

See the man page for more details.

## SBC support

Experimental support for single board computers (SBCs) via usb-gadget
has been added courtesy of Giorgi Chavchanidze.

See [usb-gadget.md](src/vkbd/usb-gadget.md) for details.

## Packages

Third party packages for the some distributions also exist. If you wish to add
yours to the list please file a PR. These are kindly maintained by community
members, no personal responsibility is taken for them.

### Alpine Linux

[keyd](https://pkgs.alpinelinux.org/packages?name=keyd) package maintained by [@jirutka](https://github.com/jirutka).

### Arch

[AUR](https://aur.archlinux.org/packages/keyd-git/) package maintained by eNV25.

# Sample Config

	[ids]

	*
	
	[main]

	leftshift = oneshot(shift)
	capslock = overload(symbols, esc)

	[symbols]

	d = ~
	f = /
	...

# Recommended config

Many users will probably not be interested in taking full advantage of keyd.
For those who seek simple quality of life improvements I can recommend the
following config:

	[ids]

	*

	[main]

	shift = oneshot(shift)
	meta = oneshot(meta)
	control = oneshot(control)

	leftalt = oneshot(alt)
	rightalt = oneshot(altgr)

	capslock = overload(control, esc)
	insert = S-insert

This overloads the capslock key to function as both escape (when tapped) and
control (when held) and remaps all modifiers to 'oneshot' keys. Thus to produce
the letter B you can now simply tap shift and then b instead of having to hold
it. Finally it remaps insert to S-insert (paste on X11).

# FAQS

## What about xmodmap/setxkbmap/*?

xmodmap and friends are display server level tools with limited functionality.
keyd is a system level solution which implements advanced features like
layering and [oneshot](https://docs.qmk.fm/#/one_shot_keys)
modifiers.  While some X tools offer similar functionality I am not aware of
anything that is as flexible as keyd.

## What about [kmonad](https://github.com/kmonad/kmonad)?

keyd was written several years ago to allow me to easily experiment with
different layouts on my growing keyboard collection. At the time kmonad did not
exist and custom keyboard firmware like
[QMK](https://github.com/qmk/qmk_firmware) (which inspired keyd) was the only
way to get comparable features. I became aware of kmonad after having published
keyd. While kmonad is a fine project with similar goals, it takes a different
approach and has a different design philosophy.

Notably keyd was written entirely in C with performance and simplicitly in
mind and will likely never be as configurable as kmonad (which is extensible
in Haskell). Having said that, it supplies (in the author's opinion) the
most valuable features in less than 2000 lines of C while providing
a simple language agnostic config format.

## Why doesn't keyd implement feature X?

If you feel something is missing or find a bug you are welcome to file an issue
on github. keyd has a minimalist (but sane) design philosophy which
intentionally omits certain features (e.g execing arbitrary executables
as root). Things which already exist in custom keyboard firmware like QMK are
good candidates for inclusion.

# Contributing

See [CONTRIBUTING](CONTRIBUTING.md).
IRC Channel: #keyd on oftc
