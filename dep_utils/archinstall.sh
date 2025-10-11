#!/bin/bash

# install all arch-dependencies

# Check if multilib is enabled
if ! grep -q "^\[multilib\]" /etc/pacman.conf; then
  echo "Enabling multilib repository..."
  sudo sed -i '/#\[multilib\]/,/#Include = \/etc\/pacman\.d\/mirrorlist/ s/^#//' /etc/pacman.conf
else
  echo "archinstall: Multilib repository already enabled."
fi

# Update package database
echo "archinstall: Updating system..."
sudo pacman -Syu --noconfirm

# Install required packages
echo "archinstall: Installing packages..."
sudo pacman -S --noconfirm \
  gcc-multilib \
  libpng \
  libx11 \
  libxext \
  lib32-libx11 \
  lib32-libxext \
  python3 \
  scons

echo "archinstall: All packages installed successfully."
