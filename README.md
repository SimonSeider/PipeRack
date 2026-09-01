# PipeRack

A very Handy tool for Routing Output Audio to Input Devices.

### Features
- Create Multiple Racks
- Invidually Adjust the Gain of each Rack Output
- See Incomming output via a Waveform Visulizer
- Rename each Racks Input and Output Name
- Pipewire and Pulseaudio Support

### Dependencies
Obviously PipeWire or PulseAudio is needed to compile it, the app is beeing developed on a Artix Linux Distro but should work out of the box with other Distro's so I'm not listing each specific package for PipeWire and PulseAudio so figure it out yourself, I belive in you :)

### Building
Firstly clone the Repository:
```
git clone https://github.com/SimonSeider/PipeRack
```
then build with:
```
cmake . -B build
make -C build -j$(nproc)
```
lastly you should be able to run PipeRack with
```
./build/piperack
```
or you can also just install it to your system
```
sudo cmake --install build
```

### Branding
As of right now there is no Icon or Specific Branding but that will be updated in the next weeks or so when i Continue to Update it.