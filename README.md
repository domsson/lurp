# lurp 

`lurp` anonymously connects to a Twitch channel of your choice 
and outputs all chat messages to `stdout`. Linux only.

<p align="center">
   <img src="https://raw.githubusercontent.com/domsson/lurp/master/example.png" alt="lurp example">
</p>

## Dependencies

- [`libtwirc`](https://github.com/domsson/libtwirc)
- `libtinfo`
- `libncurses`

## Building

1. **Install [`libtwirc`](https://github.com/domsson/libtwirc)**  
   See the [`libtwirc` Wiki](https://github.com/domsson/libtwirc/wiki)
   for installation instructions.
2. **Clone `lurp`**  
   ```
   git clone https://github.com/domsson/lurp
   ```
3. **Run the `build` script**  
   ```
   cd lurp
   chmod +x build
   ./build
   ```

## Running

    ./lurp -c CHANNEL [options...]

Example:

    ./lurp -c "#esl_csgo" -t "[%H:%M:%S]" -pb -m 4bit


### Command line options

- `-c CHANNEL`: specify the channel to join; should start with `#` 
                and be all lower-case
- `-b`: prefix usernames with `@` and/or `+` 
        for moderators and subscribers respectively
- `-d`: use display names instead of user names where available
- `-h`: print help text and exit
- `-m MODE`: manually specify the color mode, see below
- `-a`: Neatly align (left-pad) usernames and messages
- `-r`: Use server-provided timestamp instead of local time
- `-s`: print additional status information
- `-t FORMAT`: specify a timestamp format; if `-t` isn't given, 
               no timestamp will be printed
- `-v`: print version information and exit

### Color modes

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
