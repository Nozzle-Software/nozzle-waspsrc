# WaspSrc Engine

The Official WaspSrc SDK, forked off the quake engine

## Prequisites
if not alreeady, make sure your system is multi-arch `sudo dpkg --add-architecture i386 && sudo apt update` then `sudo apt update`

## Install pak0.pak
install the pak file from [here](https://archive.org/details/Quake_802)

## Installation
for installation, go to the WaspSrc directory (`./nozzle-waspsrc/WaspSrc`) and run `make`, then execute `./WaspSrc <arguments>`

> [!NOTE]
> when running, if your computer doesn't support /dev/dsp, then execute `./WaspSrc -nosound <arguments>`
