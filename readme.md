# WaspSrc Engine

The Official WaspSrc SDK, forked off the quake engine

## Prequisites
if not alreeady, make sure your system is multi-arch `sudo dpkg --add-architecture i386 && sudo apt update` then `sudo apt update`

## Install pak files
install the pak0.pak file from Internet Archive ([here](https://archive.org/download/Quake_802/zQUAKE_SW-play.zip/ID1%2FPAK0.PAK)), and put it in the `nozzle` folder

## Installation
for installation, go to the WaspSrc directory (`./nozzle-waspsrc/WaspSrc`) and run `make`, then execute `./WaspSrc <arguments>`

> [!NOTE]
> when running, if your computer doesn't support /dev/dsp, then execute `./WaspSrc -nosound <arguments>`
