# WaspSrc Engine

The Official WaspSrc SDK, forked off the quake.x11 engine

## Prequisites
if not already, make sure your system is multi-arch: `sudo dpkg --add-architecture i386 && sudo apt update` then `sudo apt update`
then, install the required dependencies with:
```bash
sudo apt update && sudo apt install \
  gcc-multilib g++-multilib libpng-dev \
  libx11-dev:i386 libxext-dev:i386
```

## Install pak files
install the pak0.pak file from Internet Archive ([here](https://archive.org/download/quake-shareware-pak/PAK0.PAK)), and put it in the `nozzle` folder. (the file had to be removed due to copyright 💾, and don't forget to rename the file name into lowercase!)

## Main Installation
for installation, go to the WaspSrc directory (`./nozzle-waspsrc/WaspSrc`) and run `make wasp | all | (leave empty)`, then execute `./WaspSrc <arguments>`

> [!NOTE]
> when running, if your computer doesn't support /dev/dsp, then execute `./WaspSrc -nosound <arguments>`
