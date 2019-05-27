# lurp 

`lurp` anonymously connects to a Twitch channel of your choice 
and outputs all chat messages to `stdout`.

Currently Linux only.

![lurp example][example.png]

## Dependencies

- [`libtwirc`](https://github.com/domsson/libtwirc)
- `libtinfo`
- `libncurses`

## Building

1. Install [`libtwirc`](https://github.com/domsson/libtwirc). 
   See the [`libtwirc` Wiki](https://github.com/domsson/libtwirc/wiki)
   for installation instructions.

2. Clone `lurp`

````
git clone https://github.com/domsson/lurp
````

3. Run the `build` script:

```
cd lurp
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
