#include <stdio.h>      // NULL, fprintf(), perror(), setlinebuf()
#include <string.h>     // strcmp()
#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <unistd.h>     // isatty(), STDOUT_FILENO
#include <errno.h>      // errno
#include <unistd.h>     // getopt() et al.
#include <sys/types.h>  // ssize_t
#include <signal.h>
#include <time.h>
#include <sys/ioctl.h>	// ioctl() to get terminal dimensions
#include "libtwirc.h"

#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_BUILD 0

#define PROJECT_NAME "lurp"
#define PROJECT_URL "https://github.com/domsson/lurp"

#define DEFAULT_HOST "irc.chat.twitch.tv"
#define DEFAULT_PORT "6667"
#define DEFAULT_TIMESTAMP "[%H:%M:%S]"
#define TIMESTAMP_BUFFER 16 


// https://en.wikipedia.org/wiki/ANSI_escape_code
#define COLOR_MODE_NONE 0  //  No colors
#define COLOR_MODE_2BIT 1  //   8 colors, 30-37
#define COLOR_MODE_4BIT 2  //  16 colors, 30-37 and 90-97
#define COLOR_MODE_8BIT 3  // 256 colors, 0-255
#define COLOR_MODE_TRUE 4  // RGB colors (true color)

#define SGR_RESET_ALL     "\033[0m"
#define SGR_BOLD_ON       "\033[1m"
#define SGR_DIM_ON        "\033[2m"
#define SGR_UNDERLINE_ON  "\033[4m"
#define SGR_BLINKING_ON   "\033[5m"
#define SGR_NEGATIVE_ON   "\033[7m"
#define SGR_INVISIBLE_ON  "\033[8m"
#define SGR_BOLD_OFF      "\033[22m"
#define SGR_UNDERLINE_OFF "\033[24m"
#define SGR_BLINKING_OFF  "\033[25m"
#define SGR_NEGATIVE_OFF  "\033[27m"
#define SGR_INVISIBLE_OFF "\033[28m"


static volatile int running; // Used to stop main loop in case of SIGINT etc
static volatile int resized; // Used to signal that the terminal size changed 
static volatile int handled; // The last signal that has been handled

struct metadata
{
	char *chan;              // Channel to join
	char *timestamp;         // Timestamp format
	int   colormode;         // Color mode
	int   align: 1;          // Align/pad nicks and messages
	int   badges : 1;        // Print sub/mod 'badges'
	int   verbose : 1;       // Print additional info
	int   twitchtime : 1;    // Use the Twitch provided timestamp
	int   displaynames : 1;  // Favor display over user names
	size_t term_width;	 // Terminal width in characters
	size_t term_height;	 // Terminal height in characters
};

/*
const char *ansi_colors[] = {
	"black",	// 30
	"red",		// 31
	"green",	// 32
	"yellow",	// 33
	"blue",		// 34
	"magenta",	// 35
	"cyan",		// 36
	"white"		// 37
};

const char *twitch_colors[] = {
	"#FF0000", // 31: Red
	"#B22222"  // 31: FireBrick
	"#D2691E", // 31/33: Chocolate
	"#008000", // 32: Green
	"#2E8B57", // 32: SeaGreen
	"#9ACD32", // 32: YellowGreen
	"#DAA520", // 33/93: GoldenRod
	"#0000FF", // 34: Blue
	"#8A2BE2", // 35: BlueViolet
	"#5F9EA0", // 36: CadetBlue
	"#FF7F50", // 91: Coral
	"#FF4500", // 91: OrangeRed
	"#00FF7F", // 92: SpringGreen
	"#1E90FF", // 94: DodgerBlue
	"#FF69B4", // 95: HotPink
};

*/

struct rgb_color 
{
	unsigned int r;
	unsigned int g;
	unsigned int b;
};

/*
 * Returns 0 if str is NULL or empty, otherwise 1.
 */
int empty(char const *str)
{
	return str == NULL || str[0] == '\0';
}

/**
 * Tries to determine the current size of the terminal window and returns them.
 * If a dimension can't be determined, width and/or height will be set to 0.
 * Returns 0 on success, -1 on error (TIOCGWINSZ not defined on your system).
 */
int termsize(size_t *w, size_t *h)
{
	// https://www.unix.com/programming/136867-curses-not-updating-lines-cols.html
	// http://www.delorie.com/djgpp/doc/libc/libc_495.html
#ifdef TIOCGWINSZ
	struct winsize ws;
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0)
        {
            *w = (ws.ws_row >= 0) ? ws.ws_col : 0;
            *h = (ws.ws_col >= 0) ? ws.ws_row : 0;
	    return 0;
        }
#endif
	*w = 0;
	*h = 0;
	return -1;
}

/*
 * Creates a timestamp string according to format, stores it in buf and returns
 * the pointer to buf for convenience. If format is NULL, buf will be set to an 
 * empty string. If timestamp is 0, the current time will be used.
 */
char *timestamp_str(char const *format, int timestamp, char *buf, size_t len)
{
	// Make sure buf contains an empty string
	buf[0] = '\0';

	// Leave it at that if format isn't given
	if (format == NULL)
	{
		return buf;
	}

	// Let's get the current time for a nice timestamp
	time_t t = timestamp ? timestamp : time(NULL);
	struct tm lt = *localtime(&t);

	// Let's run the time through strftime() for format
	strftime(buf, len, format, &lt);

	// Return the handed in buf for convenience
	return buf;
}

// TODO This needs to be improved, some matches are shit
// It is a slightly changed version of this snippet:
// https://stackoverflow.com/questions/1988833/converting-color-to-consolecolor/29192463#29192463
int rgb_to_4bit(const struct rgb_color *rgb)
{	
	unsigned char ansi = 0;
	ansi |= (rgb->r > 64 && (rgb->g < 196 && rgb->b < 196)) ? 1 : 0;  // Red bit
	ansi |= (rgb->g > 64 && (rgb->r < 196 && rgb->b < 196)) ? 2 : 0;  // Green bit
	ansi |= (rgb->b > 64 && (rgb->r < 196 && rgb->g < 196)) ? 4 : 0;  // Blue bit
	ansi += (rgb->r > 128 || rgb->g > 128 || rgb->b > 128) ? 90 : 30; // Bright bit

	return ansi;
}

// TODO this also doesn't seem to yield accurate results
// https://gist.github.com/MicahElliott/719710
int rgb_to_8bit(const struct rgb_color *rgb)
{
	return (rgb->r/255.0 * 36) + (rgb->g/255.0 * 6) + (rgb->b/255.0) + 16;
}

/*
 * Convert a hex color string ("#RRGGBB") to a rgb_color struct.
 */
struct rgb_color hex_to_rgb(const char *hex)
{
	struct rgb_color rgb = { 0 };
	sscanf(hex[0]=='#' ? hex+1 : hex, "%02x%02x%02x", &rgb.r, &rgb.g, &rgb.b);
	return rgb;
}

/*
 * Returns a rgb_color struct initialized to a random color.
 */
struct rgb_color random_color()
{
	struct rgb_color rgb = { 0 };
	srand(time(NULL));
	rgb.r = rand() % 256;
	rgb.g = rand() % 256;
	rgb.b = rand() % 256;
	return rgb;
}

/*
 * Called once the connection has been established. This does not mean we're
 * authenticated yet, hence we should not attempt to join channels yet etc.
 */
void handle_connect(twirc_state_t *s, twirc_event_t *evt)
{
	struct metadata *meta = twirc_get_context(s);
	if (meta->verbose)
	{
		fprintf(stderr, "*** Connected\n");
	}
}

/*
 * Called once we're authenticated. This is where we can join channels etc.
 */
void handle_welcome(twirc_state_t *s, twirc_event_t *evt)
{
	struct metadata *meta = twirc_get_context(s);
	if (meta->verbose)
	{
		fprintf(stderr, "*** Authenticated\n");
	}

	// Let's join the specified channel
	twirc_cmd_join(s, meta->chan);
}

/*
 * Called once we see a user join a channel we're in. This also fires for when
 * we join a channel, in which case 'evt->origin' will be our own username.
 */
void handle_join(twirc_state_t *s, twirc_event_t *evt)
{
	twirc_login_t *login = twirc_get_login(s);

	if (!evt->origin)
	{
		return;
	}
	if (!login->nick)
	{
		return;
	}
	if (strcmp(evt->origin, login->nick) != 0)
	{
		return;
	}

	struct metadata *meta = twirc_get_context(s);
	if (meta->verbose)
	{
		fprintf(stderr, "*** Joined %s\n", evt->channel);
	}
}

char *color_prefix(int colormode, const struct rgb_color *rgb, char *buf, size_t len)
{
	// Initialize to empty string
	buf[0] = '\0';

	if (colormode == COLOR_MODE_NONE || rgb == NULL)
	{
		return buf;
	}

	if (colormode == COLOR_MODE_2BIT)
	{
		// 8 colors, only 30-37
		// \033[<color>m
		int c = rgb_to_4bit(rgb);
		snprintf(buf, len, "\033[%dm", c >= 90 ? c-60 : c);
		return buf;
	}
	
	if (colormode == COLOR_MODE_4BIT)
	{
		// 16 colors
		// \033[<color>m
		int c = rgb_to_4bit(rgb);
		snprintf(buf, len, "\033[%dm", c);
		return buf;
	}

	if (colormode == COLOR_MODE_8BIT)
	{
		// 256 colors
		// \033[38;5;<color>m
		int c = rgb_to_8bit(rgb);
		snprintf(buf, len, "\033[38;5;%dm", c);
		return buf;
	}

	if (colormode == COLOR_MODE_TRUE)
	{
		// ~16 million colors
		// \033[38;2;<r>;<g>;<b>m
		snprintf(buf, len, "\033[38;2;%d;%d;%dm", rgb->r, rgb->g, rgb->b);
		return buf;
	}

	return buf;
}

char *color_suffix(int colormode, const struct rgb_color *rgb, char *buf, size_t len)
{
	buf[0] = '\0';

	if (colormode == COLOR_MODE_NONE || rgb == NULL)
	{
		return buf;
	}

	snprintf(buf, len, "\033[0m");
	return buf;	
}

/*
 * Given the "badges" tag, determines if the origin has moderator permissions.
 * This means the origin (the user who caused the event) is either a moderator
 * or the broadcaster of the channel the event occurred in.
 * Returns 1 for mods, 0 for non-mods, -1 on error.
 */
int is_mod(char const *badges)
{
	if (badges == NULL)
	{
		return -1;
	}

	return strstr(badges, "moderator") || strstr(badges, "broadcaster");
}

/*
 * Given the "badges" tag, determines if the origin is a subscriber.
 * Returns 1 for subs, 0 for non-subs, -1 on error.
 */
int is_sub(char const *badges)
{
	if (badges == NULL)
	{
		return -1;
	}

	return strstr(badges, "subscriber") ? 1 : 0;
}

size_t print_msg_head(char const *ts, char const *badges, char const *nick, int cmode, char const *hex, int action, size_t tw)
{
	struct rgb_color rgb = hex_to_rgb(hex); 

	char col_prefix[32];
	color_prefix(cmode, &rgb, col_prefix, 32);
	char col_suffix[8];
	color_suffix(cmode, &rgb, col_suffix, 8);

	char name[64];
	snprintf(name, 64, "%s%s", badges, nick);

	//                      .-- timestamp
	//                      |            .-- 1 for space after timestamp       
	//                      |            |        .-- badge char (1) + nick (max 25)
	//                      |            |        |    .-- ": " or "  "
	//                      |            |        |    |
	size_t header_len = strlen(ts) + !empty(ts) + 26 + 2;
	int padding = header_len > tw ? 0 : 26;

	// We only print the message header for now:
	//
	//                        .-- timestamp
	//                        | .-- space after timestamp
	//                        | | .-- color start
	//                        | | |  .-- padded nick
	//                        | | |  | .-- color end
	//                        | | | /| | .-- ": "
	//                        | | | || | | 
	int p = fprintf(stdout, "%s%s%s%*s%s%s",
			ts,
			empty(ts) ? "" : " ",
			col_prefix,
			padding,
			name,
			col_suffix,
			action ? "  " : ": "
	);

	return p - strlen(col_prefix) - strlen(col_suffix);
}

size_t print_msg_body(char *msg, int cmode, char const *hex, size_t tw, int pad)
{
	struct rgb_color rgb = hex_to_rgb(hex); 

	char col_prefix[32];
	color_prefix(cmode, &rgb, col_prefix, 32);
	char col_suffix[8];
	color_suffix(cmode, &rgb, col_suffix, 8);

	if (tw == 0)
	{
		return fprintf(stdout, "%s%s%s\n", col_prefix, msg, col_suffix);
	}

	// If this is an action message ("/me", cmode will be != 0), we color it 
	fprintf(stdout, "%s", col_prefix);

	// TODO this smells, I feel like we can do this with a third of the code
	// TODO it also doesn't work, lul

	int i = 0;                    // words printed total
	int w = 0;                    // words printed on the current line
	char *tok = NULL;             // current word to print
	size_t tok_len = 0;           // length of current token
	size_t width = tw - pad;      // width available per line
	size_t width_left = width;    // space left in the current line

	for (; (tok = strtok(i == 0 ? msg : NULL, " ")); ++i)
	{
		tok_len = strlen(tok);

		// In case the word will never fit on the terminal...
		if (tok_len > width)
		{
			// ...we just print it and fuck up alignment
			// TODO obviously, we should instead just split
			// the word up, print the remainder on the next line
			fprintf(stdout, "%s", tok);
			w++;
		}

		// In case there is enough space left for the word... 
		else if (tok_len <= width_left)
		{
			// ...we print it, maybe with a space before it
			fprintf(stdout, "%s%s", w > 0 ? " " : "", tok);
			width_left -= tok_len + (w > 0);
			w++;
		}	

		// Not enough space for the word left on this line...
		else
		{
			// ...so we need a line break and padding
			fprintf(stdout, "\n");
			// And now reset the available width size
			width_left = width;
			fprintf(stdout, "%*s%s", pad, "", tok);
			width_left = width - tok_len;
			w++;
		}
	}

	// Finally, add the last line break (and end the color code)
	fprintf(stdout, "%s\n", col_suffix);

	return 0;
}

void print_privmsg(char const *ts, char const *badges, char const *nick, char *msg, int cmode, char const *hex) 
{
	print_msg_head(ts, badges, nick, cmode, hex, 0, 0);
	print_msg_body(msg, COLOR_MODE_NONE, hex, 0, 0);
}

// TODO
void print_privmsg_aligned(char const *ts, char const *badges, char const *nick, char *msg, int cmode, char const *hex, size_t tw)
{
	size_t pad = print_msg_head(ts, badges, nick, cmode, hex, 0, tw);
	print_msg_body(msg, COLOR_MODE_NONE, hex, tw, pad);	
}

void print_action(char const *ts, char const *badges, char const *nick, char const *msg, int cmode, char const *hex)
{
	struct rgb_color rgb = hex_to_rgb(hex); 

	char col_prefix[32];
	color_prefix(cmode, &rgb, col_prefix, 32);
	char col_suffix[8];
	color_suffix(cmode, &rgb, col_suffix, 8);

	//                .-- timestamp
	//                | .-- space after timestamp
	//                | |  .-- color start
	//                | | |   .-- badges
	//                | | |   | .-- nickname
	//                | | |   | |  .-- message
	//                | | |   | |  | .-- color end
	//                | | |   | |  | |
	fprintf(stdout, "%s%s%s* %s%s %s%s\n",
			ts ? ts : "",
			ts ? " " : "",
			col_prefix,
			badges,
			nick,
			msg,
			col_suffix
	);
}

// TODO
void print_action_aligned(char const *ts, char const *badges, char const *nick, char const *msg, int cmode, char const *hex, size_t tw)
{
}

void handle_message(twirc_state_t *s, twirc_event_t *evt)
{
	struct metadata *meta = twirc_get_context(s);

	char const *color  = twirc_get_tag_value(evt->tags, "color");
	char const *badges = twirc_get_tag_value(evt->tags, "badges");
	char const *dname  = twirc_get_tag_value(evt->tags, "display-name");
	char const *tmits  = twirc_get_tag_value(evt->tags, "tmi-sent-ts");

	// Prepare nickname string
	char nick[TWIRC_NICK_SIZE];
	snprintf(nick, TWIRC_NICK_SIZE, "%s", meta->displaynames && !empty(dname) ? dname : evt->origin);

	// Prepare badges string
	char badge[2];
	snprintf(badge, 2, "%s", is_mod(badges) == 1 ? "@" : (is_sub(badges) == 1 ? "+" : ""));

	// Prepare color string
	char hex[8];
       	snprintf(hex, 8, "%s", empty(color) ? "#FFFFFF" : color);

	// Prepare timestamp string
	char timestamp[TIMESTAMP_BUFFER];
	int ts = meta->twitchtime ? atoi(tmits)/1000 : 0;
	timestamp_str(meta->timestamp, ts, timestamp, TIMESTAMP_BUFFER);

	if (evt->ctcp)
	{
		if (meta->align)
		{
			print_action_aligned(timestamp, badge, nick, evt->message, meta->colormode, hex, meta->term_width);
		}
		else
		{
			print_action(timestamp, badge, nick, evt->message, meta->colormode, hex);
		}
	}
	else
	{
		if (meta->align)
		{
			print_privmsg_aligned(timestamp, badge, nick, evt->message, meta->colormode, hex, meta->term_width);
		}
		else
		{
			print_privmsg(timestamp, badge, nick, evt->message, meta->colormode, hex);
		}

	}
}

/*
 * Called when a loss of connection has been detected. This could be due to 
 * a connection error or because Twitch closed the connection on us.
 */
void handle_disconnect(twirc_state_t *s, twirc_event_t *evt)
{
	struct metadata *meta = twirc_get_context(s);
	if (meta->verbose)
	{
		fprintf(stderr, "*** Disconnected\n");
	}
	running = 0;
}

/*
 * Let's handle CTRL+C by stopping our main loop and tidying up.
 */
void sigint_handler(int sig)
{
	running = 0;
	handled = sig;
}

/**
 * Handles a window size change signal.
 */
void sigwin_handler(int sig)
{
	resized = 1;
	handled = sig;
}


/**
 * Prints the program's name and version number.
 */
void version()
{
	fprintf(stdout, "%s version %d.%d.%d - %s\n",
				PROJECT_NAME,
				VERSION_MAJOR,
				VERSION_MINOR,
				VERSION_BUILD,
				PROJECT_URL);
}

/**
 * Prints usage information.
 */
void help(char *invocation)
{
	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "\t%s -c CHANNEL [OPTIONS...]\n", invocation);
	fprintf(stdout, "\tNote: the channel should start with '#' and be all lower-case.\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Options:\n");
	fprintf(stdout, "\t-a Neatly align (left-pad) usernames and messages.\n");
	fprintf(stdout, "\t-b Mark subscribers and mods with + and @ respectively.\n");
	fprintf(stdout, "\t-d Use display names instead of user names where available.\n");
	fprintf(stdout, "\t-h Print this help text and exit.\n");
	fprintf(stdout, "\t-m MODE Set the color mode: 'true', '8bit', '4bit', '2bit' or 'none'.\n");
	fprintf(stdout, "\t-r Use the server-supplied timestamp instead of the local time.\n");
	fprintf(stdout, "\t-s Print additional status information to stderr.\n");
	fprintf(stdout, "\t-t FORMAT Enable timestamps, using the specified format.\n");
	fprintf(stdout, "\t-v Print version information and exit.\n");
	fprintf(stdout, "\n");
	version();
}

/*
 * Tries to determine the terminal's color capabilities by looking at the 
 * TERM and COLORTERM environment variables and, based on this, returns an
 * educated guess as to which is the highest supported color mode. In case
 * no hints to the color mode can be found in either variable, this function
 * errors on the safe side and returns COLOR_MODE_NONE.
 */
int detect_color_mode()
{
	// Don't output colors if we're not talking to a terminal
	if (!isatty(fileno(stdout)))
	{
		return COLOR_MODE_NONE; 
	}

	// Check terminal name for suffix '-m' or '-256'
	char *term  = getenv("TERM");
	if (term && strstr(term, "-m\0"))
	{
		// -m = Monochrome, no colors
		return COLOR_MODE_NONE;
	}
	if (term && strstr(term, "-m-"))
	{
		return COLOR_MODE_NONE;
	}
	if (term && strstr(term, "-8c"))
	{
		return COLOR_MODE_2BIT;
	}
	if (term && strstr(term, "-16c"))
	{
		return COLOR_MODE_4BIT;
	}
	if (term && strstr(term, "-256"))
	{
		return COLOR_MODE_8BIT;
	}

	// Check COLORTERM env var for 'truecolor' or '24bit'
	// https://gist.github.com/XVilka/8346728
	char *cterm = getenv("COLORTERM");
	if (cterm && strstr(cterm, "truecolor"))
	{
		return COLOR_MODE_TRUE;
	}
	if (cterm && strstr(cterm, "24bit"))
	{
		return COLOR_MODE_TRUE;
	}

	return COLOR_MODE_NONE;
}

int color_mode(const char *mode, int fallback)
{
	if (strcmp(mode, "true") == 0)
	{
		return COLOR_MODE_TRUE;
	}
	if (strcmp(mode, "8bit") == 0)
	{
		return COLOR_MODE_8BIT;
	}
	if (strcmp(mode, "4bit") == 0)
	{
		return COLOR_MODE_4BIT;
	}
	if (strcmp(mode, "2bit") == 0)
	{
		return COLOR_MODE_2BIT;
	}
	if (strcmp(mode, "none") == 0)
	{
		return COLOR_MODE_NONE;
	}
	return fallback;
}


/*
 * Main - this is where we make things happen!
 */
int main(int argc, char **argv)
{
	// Set stdout to line buffered
	setlinebuf(stdout);
	
	// Get a metadata struct	
	struct metadata m = { 0 };
	
	// Attempt to detect color mode (errs on safe side)
	m.colormode = detect_color_mode();
	
	// Process command line options
	opterr = 0;
	int o;
	while ((o = getopt(argc, argv, "abc:dhm:rst:v")) != -1)
	{
		switch(o)
		{
			case 'a':
				m.align = 1;
				break;
			case 'b':
				m.badges = 1;
				break;
			case 'c':
				m.chan = optarg;
				break;
			case 'd':
				m.displaynames = 1;
				break;
			case 'h':
				help(argv[0]);
				return EXIT_SUCCESS;
			case 'm':
				m.colormode = color_mode(optarg, m.colormode);
				break;
			case 'r':
				m.twitchtime = 1;
				break;
			case 's':
				m.verbose = 1;
				break;
			case 't':
				m.timestamp = optarg;
				break;
			case 'v':
				version();
				return EXIT_SUCCESS;
		}
	}

	// Abort if no channel name was given	
	if (m.chan == NULL)
	{
		help(argv[0]);
		return EXIT_FAILURE;
	}

	if (m.verbose)
	{
		fprintf(stderr, "*** Initializing\n");
	}

	// Get the terminal size
	termsize(&(m.term_width), &(m.term_height));
	
	// Make sure we still do clean-up on SIGINT (ctrl+c)
	// and similar signals that indicate we should quit.
	struct sigaction sa_int = {
		.sa_handler = &sigint_handler
	};
	
	// These might return -1 on error, but we'll ignore that for now
	sigaction(SIGINT,  &sa_int, NULL);
	sigaction(SIGQUIT, &sa_int, NULL);
	sigaction(SIGTERM, &sa_int, NULL);

	struct sigaction sa_win = {
		.sa_handler = &sigwin_handler
	};

	// Again, this might return -1 on error, handle it eventually
	sigaction(SIGWINCH, &sa_win, NULL);

	// Create libtwirc state instance
	twirc_state_t *s = twirc_init();

	if (s == NULL)
	{
		fprintf(stderr, "Error initializing libtwirc, exiting\n");
		return EXIT_FAILURE;
	}
	
	// Save the metadata in the state
	twirc_set_context(s, &m);

	// We get the callback struct from the libtwirc state
	twirc_callbacks_t *cbs = twirc_get_callbacks(s);

	// We assign our handlers to the events we are interested int
	cbs->connect         = handle_connect;
	cbs->welcome         = handle_welcome;
	cbs->join            = handle_join;
	cbs->action          = handle_message;
	cbs->privmsg         = handle_message;
	cbs->disconnect      = handle_disconnect;
	
	// Connect to the IRC server
	if (twirc_connect_anon(s, DEFAULT_HOST, DEFAULT_PORT) != 0)
	{
		fprintf(stderr, "Error connecting, exiting\n");
		return EXIT_FAILURE;
	}

	// Main loop - we call twirc_tick() every go-around, as that's what 
	// makes the magic happen. The 1000 is a timeout in milliseconds that
	// we grant twirc_tick() to do its work - in other words, we'll give 
	// it 1 second to wait for and process IRC messages, then it will hand 
	// control back to us. If twirc_tick() detects a disconnect or error,
	// it will return -1, otherwise it will return 0 and we can go on!

	running = 1;
	while (twirc_tick(s, 1000) == 0 && running == 1)
	{
		// If we caught a window resize signal, fetch the new size
		if (resized)
		{
			termsize(&(m.term_width), &(m.term_height));
			fprintf(stderr, "Term size changed: %zu x %zu\n", m.term_width, m.term_height);
			resized = 0;
		}
	}

	if (twirc_get_last_error(s) < 0)
	{
		fprintf(stderr, "*** Last error: %d\n", twirc_get_last_error(s));
	}

	// twirc_kill() is a convenience functions that calls two functions:
	// - twirc_disconnect(), which makes sure the connection was closed
	// - twirc_free(), which frees the libtwirc state, so we don't leak
	twirc_kill(s);

	// That's all, wave good-bye!
	return EXIT_SUCCESS;
}

