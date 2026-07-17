# Linux Instructions

Install the dependencies:

```sh
sudo apt install libasound2-dev libjack-jackd2-dev \
    libcurl4-openssl-dev  \
    libfreetype-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
    libwebkit2gtk-4.1-dev \
    libglu1-mesa-dev mesa-common-dev \
    clang cmake ninja
```

or for RPM based distro (e.g. Fedora):

```sh
sudo dnf install \
    freetype-devel fontconfig-devel \
    mesa-libGLU-devel \
    libcurl-devel \
    webkit2gtk4.1-devel \
    alsa-lib-devel pipewire-jack-audio-connection-kit-devel \
    clang clang-tools-extra cmake ninja
```

If you are on some other distro you can probably figure out the missing libraries by running `./configure.sh` and searching for the missing libraries using your package manager.

The default makefiles based build does not support multiple different configurations. Use the following commands to use the ninja multi config build to be able to select different buildtypes (Release/Debug...):

```
cmake -Bbuild -G'Ninja Multi-Config'
cmake --build build --target <target>_<format> --config <Debug/Release> -j<number of jobs>
``` 


## Clangd

For clangd you should run the following after configuring and building:

```sh
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -B build
```

## Issues

The standalone version seems to be broken on Linux (`./build.sh <plugin> Standalone`). Luckily, VST3 works fine in Reaper, Ardour and Carla.

## Debugging

For debugging you can use [Pluginval](https://github.com/Tracktion/pluginval/tree/develop) or Ardour by launching it with `--gdb` flag.

To make the DBG macro work you might need to run the following CMAKE command:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Debug
```

and manually add `JUCE_DEBUG=1` to the `CMakeLists.txt`:

```diff
 if (LINUX)
-    set(linux_defines JUCE_USE_CURL=0 JUCE_JACK=1)
+    set(linux_defines JUCE_USE_CURL=0 JUCE_JACK=1 JUCE_DEBUG=1)
 endif()
```

## LV2

You can enable lv2 by adding it to the `FORMATS` list and adding an `LV2URI` in `CMakeLists.txt`.

```cmake
# Add juce plugin target
juce_add_plugin(${target}
    ...
    FORMATS Standalone VST3 AU LV2
    LV2URI http://example.com/${AP_PROD_NAME}
    ...
```

and then building using `./build.sh <plugin> LV2`

However, in testing the JUCE's LV2 format was ***very*** broken and refused to even load in some hosts. So, it is recommended to stick with VST3.
