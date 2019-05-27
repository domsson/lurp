# lurp 

Lurp anonymously connects to a Twitch channel of your choice 
and outputs all chat messages to `stdout`.

## Dependencies

- [`libtwirc`](https://github.com/domsson/libtwirc)

## Building

1. Clone `lurp` and [`libtwirc`](https://github.com/domsson/libtwirc):

````
git clone https://github.com/domsson/twircclient
git clone https://github.com/domsson/libtwirc
````

2. Build `libtwirc` with the provided `build-shared` script. 
   It will output the `.so` and `.h` file into the `lib` subdirectory:

```
cd libtwirc
chmod +x build-shared
./build-shared
```

3. Install `libtwirc`. This will vary depending on your distro and machine, 
   as the include paths aren't the same for all. You'll have to look up where 
   your distro wants shared library and header files. In any case, after 
   copying the files to their appropriate directories, you should call 
   `ldconfig` so that your system learns about the new library. For my amd64 
   Debian install, this does the trick (requires super user permissions):

```
cp lib/libtwirc.so /usr/lib/x86_64-linux-gnu/
cp lib/libtwirc.h /usr/include/
ldconfig -v -n /usr/lib
```

4. Run the `build` script in the `lurp` directory:

```
cd ../lurp
chmod +x build
./build
```

## Running

```
./lurp -c CHANNEL [options...]
````

Example:

```
./lurp -c "#esl_csgo" -t "[%H:%M:%S]" -pb -m 4bit
```

The following command line options are available:

- `-c CHANNEL`: specify the channel to join; should start with `#` 
                and be all lower-case
- `-t FORMAT`: specify a timestamp format; if `-t` isn't given, 
               no timestamp will be printed
- `-p`: use padding to align all user names and messages neatly
- `-b`: prefix usernames with `@` and/or `+` 
        for moderators and subscribers respectively
- `-m MODE`: manually specify the color mode, see below.

## Color modes

`lurp` makes an educated guess as to how many colors your terminal 
supports and tries to make use of that. However, you can override this 
by explicitly specifying the color mode yourself. The following options 
are available:

- `none`: monochrome, no colors
- `2bit`: 8 colors
- `4bit`: 16 colors
- `8bit`: 256 colors
- `true`: true color (RGB, 16777216 colors)

Currently, the color conversion algorithms used by `lurp` are clumsy
at best and `4bit` mode actually seems to yield the most pleasent results.
On top of that, `4bit` seems to be pretty widely supported.
