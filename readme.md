# WaspSrc Engine

<img src="https://github.com/Nozzle-Software/nozzle-waspsrc/blob/master/res/quake-logo.png?raw=true" alt="Alt Text" width="224.5" height="309">

The Official WaspSrc SDK, forked off the quake.x11 engine

## Prequisites
if not already, make sure your system is multi-arch: `sudo dpkg --add-architecture i386` then `sudo apt update`
then, install the required dependencies with:
```bash
sudo apt update && sudo apt install \
  gcc-multilib g++-multilib libpng-dev \
  libx11-dev:i386 libxext-dev:i386 \
  python3 scons \
```

** OR **

* Use the `make depinstall_ub` or the `make depinstall_arch`
* manually run the `./dep_utils/ubuntuinstall.sh` or `./dep_utils/archinstall.sh` (if not already, chmod them!)
* I hope the windows compilation doesn't burn in hell (R.I.P. `WaspSrc.exe`)

*** More support of different operating systems will come soon! ***

## Build Process
* For some other [UNIX based] operating systems, there is a SConstruct file for compilation, so just use `scons` (or, for linux especially, you can use Makefile by: `make`, but scons is recommended.)

> [!WARNING]
> I wouldn't recomend making with `make` and / or cleaning with `scons`

## Install pak files
install the pak0.pak file from Internet Archive ([here](https://archive.org/download/quake-shareware-pak/PAK0.PAK)), and put it in the `nozzle` folder. (the file had to be removed due to copyright 💾. And don't forget to rename the file name into lowercase!)

-- OR -- 
future pak0.pak files would be made specificly for the WaspSrc engine! (Use those in the future. Sadly it is the present...)

## Main Installation
for installation, go to the WaspSrc directory (`./nozzle-waspsrc/WaspSrc`) and run `make wasp | all | (leave empty)`, then execute `./WaspSrc <arguments>`

> [!NOTE]
> Audio support and migration from /dev/dsp/ is in progress!
