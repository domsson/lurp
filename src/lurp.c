#include <stdio.h>      // NULL, fprintf(), perror(), setlinebuf()
#include <string.h>     // strcmp()
#include <stdlib.h>     // NULL, EXIT_FAILURE, EXIT_SUCCESS
#include <unistd.h>     // isatty()
#include <errno.h>      // errno
#include <unistd.h>     // getopt() et al.
#include <sys/types.h>  // ssize_t
#include <signal.h>
#include <time.h>
#include <curses.h>	// terminfo capability access
#include <term.h>	// terminfo capability access
#include "libtwirc.h"

#define VERSION_MAJOR 0
#define VERSION_MINOR 1
#define VERSION_BUILD 0

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
static volatile int handled; // The last signal that has been handled

struct metadata
{
	char *chan;              // Channel to join
	char *timestamp;         // Timestamp format
	int   colormode;         // Color mode
	int   padding : 1;       // Pad nicknames to align them
	int   badges : 1;        // Print sub/mod 'badges'
	int   verbose : 1;       // Print additional info
	int   displaynames : 1;  // Favor display over user names
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
 * Constructs a timestamp according to the timestamp format saved in the metadata
 * that should be in the state's context and adds a space to it, so it can be 
 * directly used as a prefix for chat messages. If there is no timestamp format,
 * it simply puts an empty string into the provided buffer.
 */
char *timestamp_prefix(twirc_state_t *s, char *buf, size_t len)
{
	// Initialize to empty string
	buf[0] = '\0';

	// Get the metadata from the state context
	struct metadata *meta = twirc_get_context(s);

	// Stop if we're not supposed to show a timestamp
	if (!meta->timestamp)
	{
		return buf;
	}

	// Let's get the current time for a nice timestamp
	time_t t = time(NULL);
	struct tm lt = *localtime(&t);

	// Let's run the time through strftime() for format
	strftime(buf, len - 1, meta->timestamp, &lt);

	// Let's make sure there is a space after the string
	size_t actual_len = strlen(buf);
	buf[actual_len] = ' '; // Add a space, overwriting null terminator
	buf[actual_len+1] = 0; // Add null terminator again

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
int is_mod(twirc_tag_t *badges)
{
	if (badges == NULL || badges->value == NULL)
	{
		return -1;
	}

	if (strstr(badges->value, "moderator/"))
	{
		return 1;
	}

	if (strstr(badges->value, "broadcaster/"))
	{
		return 1;
	}

	return 0;
}

/*
 * Given the "badges" tag, determines if the origin is a subscriber.
 * Returns 1 for subs, 0 for non-subs, -1 on error.
 */
int is_sub(twirc_tag_t *badges)
{
	if (badges == NULL || badges->value == NULL)
	{
		return -1;
	}

	if (strstr(badges->value, "subscriber/"))
	{
		return 1;
	}

	return 0;
}

/*
 * Called when a user sends a message to a channel. In other words, chat!
 * 'evt->origin' will contain the username of the person who sent the message,
 * 'evt->channel' will contain the channel the message was sent to,
 * 'evt->message' will contain the actual chat message.
 */
void handle_privmsg(twirc_state_t *s, twirc_event_t *evt)
{
	struct metadata *meta = twirc_get_context(s);

	twirc_tag_t *color_tag  = twirc_get_tag_by_key(evt->tags, "color");
	twirc_tag_t *badges_tag = twirc_get_tag_by_key(evt->tags, "badges");
	twirc_tag_t *dname_tag  = twirc_get_tag_by_key(evt->tags, "display-name");

	char status = is_mod(badges_tag)==1 ? '@' : (is_sub(badges_tag)==1 ? '+' : ' ');

	char nick[TWIRC_NICK_SIZE];
	sprintf(nick, "%c%s", status, meta->displaynames && dname_tag && dname_tag->value ? dname_tag->value : evt->origin);

	char timestamp[TIMESTAMP_BUFFER];
	timestamp_prefix(s, timestamp, TIMESTAMP_BUFFER);

	struct rgb_color white = { 255, 255, 255 };
	struct rgb_color rgb = color_tag && color_tag->value ? hex_to_rgb(color_tag->value) : white; 

	char col_prefix[32];
	color_prefix(meta->colormode, &rgb, col_prefix, 32);
	char col_suffix[8];
	color_suffix(meta->colormode, &rgb, col_suffix, 8);

	int prefix_len = strlen(timestamp) + (meta->padding ? 26 : strlen(nick)) + 2;
	int width = tigetnum("cols") - prefix_len;
	int msg_len = strlen(evt->message);

	fprintf(stdout, "%s%s%*s%s: %.*s\n",
				timestamp,
				col_prefix,
				meta->padding * -26, // 25 = max nick length on Twitch, +1 for 'badge' 
				nick,
				col_suffix,
				width,
				evt->message);

	// TODO this is working quite nicely, but it has one issue: words will
	//      be ripped apart. instead, we should implement some logic that
	//      makes sure that the message will only be split on spaces.

	int offset = width;
	while (offset < msg_len)
	{
		fprintf(stderr, "%*s%.*s\n", prefix_len, " ", width, evt->message + offset);
		offset += width;
	}
}

/*
 * Called when a user uses the "/me" command in a channel.
 * This is pretty much the same as the privmsg, just that the output is usually
 * stylized to differentiate it from regular chat messages.
 */
void handle_action(twirc_state_t *s, twirc_event_t *evt)
{
	struct metadata *meta = twirc_get_context(s);

	twirc_tag_t *color_tag  = twirc_get_tag_by_key(evt->tags, "color");
	twirc_tag_t *badges_tag = twirc_get_tag_by_key(evt->tags, "badges");
	twirc_tag_t *dname_tag  = twirc_get_tag_by_key(evt->tags, "display-name");

	char status = is_mod(badges_tag)==1 ? '@' : (is_sub(badges_tag)==1 ? '+' : ' ');

	char nick[TWIRC_NICK_SIZE];
	sprintf(nick, "%c%s", status, meta->displaynames && dname_tag && dname_tag->value ? dname_tag->value : evt->origin);

	char timestamp[TIMESTAMP_BUFFER];
	timestamp_prefix(s, timestamp, TIMESTAMP_BUFFER);

	struct rgb_color white = { 255, 255, 255 };
	struct rgb_color rgb = color_tag && color_tag->value ? hex_to_rgb(color_tag->value) : white; 

	char col_prefix[32];
	color_prefix(meta->colormode, &rgb, col_prefix, 32);
	char col_suffix[8];
	color_suffix(meta->colormode, &rgb, col_suffix, 8);

	// TODO we also need to split up longs lines and left-pad additional lines.
	//      note, however, that here, we have the additional challenge of having
	//      to put the col_suffix at the very end.

	fprintf(stdout, "%s%s%*s %s%s%s\n",
				timestamp,
				col_prefix,
				meta->padding * -26, 
				nick,
				meta->padding ? " " : "", 
				evt->message,
				col_suffix);

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

void version()
{
	fprintf(stdout, "twitch-dump version %d.%d.%d - %s\n",
				VERSION_MAJOR,
				VERSION_MINOR,
				VERSION_BUILD,
				PROJECT_URL);
}

void help(char *invocation)
{
	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "\t%s [OPTION...] -c CHANNEL\n", invocation);
	fprintf(stdout, "\t Note: the channel should start with '#'\n");
	fprintf(stdout, "\n");
	fprintf(stdout, "Options:\n");
	fprintf(stdout, "\t-b Mark subscribers and mods with + and @ respectively.\n");
	fprintf(stdout, "\t-d Use display names instead of user names where available.\n");
	fprintf(stdout, "\t-h Print this help text and exit.\n");
	fprintf(stdout, "\t-m MODE Set the color mode: 'true', '8bit', '4bit', '2bit' or 'none'.\n");
	fprintf(stdout, "\t-p Left-pad usernames to align them.\n");
	fprintf(stdout, "\t-s Print additional status information to stderr.\n");
	fprintf(stdout, "\t-t FORMAT Enable timestamps, optionally specifying the format.\n");
	fprintf(stdout, "\t-v Print version information and exit.\n");
	fprintf(stdout, "\n");
	version();
}

/*
 * Assumes that setuppterm() has been called before calling this method
 */
int detect_color_mode()
{
	// Don't output colors if we're not talking to a terminal
	if (!isatty(fileno(stdout)))
	{
		return COLOR_MODE_NONE; 
	}

	// TODO TEMP, delete later
	//fprintf(stderr, "%d x %d (width x height)\n", tigetnum("cols"), tigetnum("lines"));

	// Query number of colors from terminfo	
	int num_colors = tigetnum("colors");
	if (num_colors == 16777216)
	{
		return COLOR_MODE_TRUE;
	}
	if (num_colors >= 256)
	{
		return COLOR_MODE_8BIT;
	}
	if (num_colors >= 16)
	{
		return COLOR_MODE_4BIT;
	}
	if (num_colors >= 8)
	{
		return COLOR_MODE_2BIT;
	}
	if (num_colors >= 1)
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
	if (term && strstr(term, "-256"))
	{
		return COLOR_MODE_4BIT;
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
	
	// Initialize terminfo database access
	setupterm(NULL, fileno(stdout), (int *)0);

	// Get a metadata struct	
	struct metadata m = { 0 };
	
	// Attempt to detect color mode (errs on safe side)
	m.colormode = detect_color_mode();
	
	// Process command line options
	opterr = 0;
	int o;
	while ((o = getopt(argc, argv, "c:t:m:bdpsvh")) != -1)
	{
		switch(o)
		{
			case 'c':
				m.chan = optarg;
				break;
			case 'b':
				m.badges = 1;
				break;
			case 'd':
				m.displaynames = 1;
				break;
			case 'm':
				m.colormode = color_mode(optarg, m.colormode);
				break;
			case 'p':
				m.padding = 1;
				break;
			case 't':
				m.timestamp = optarg;
				break;
			case 's':
				m.verbose = 1;
				break;
			case 'v':
				version();
				return EXIT_SUCCESS;
			case 'h':
				help(argv[0]);
				return EXIT_SUCCESS;
		}
	}

	// Abort if no channel name was given	
	if (m.chan == NULL)
	{
		fprintf(stderr, "No channel specified, exiting\n");
		return EXIT_FAILURE;
	}

	if (m.verbose)
	{
		fprintf(stderr, "*** Initializing\n");
	}
	
	// Make sure we still do clean-up on SIGINT (ctrl+c)
	// and similar signals that indicate we should quit.
	struct sigaction sa_int = {
		.sa_handler = &sigint_handler
	};
	
	// These might return -1 on error, but we'll ignore that for now
	sigaction(SIGINT, &sa_int, NULL);
	sigaction(SIGQUIT, &sa_int, NULL);
	sigaction(SIGTERM, &sa_int, NULL);

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
	cbs->action          = handle_action;
	cbs->privmsg         = handle_privmsg;
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
		// Nothing to do here - it's all done via the event handlers
	}

	// twirc_kill() is a convenience functions that calls two functions:
	// - twirc_disconnect(), which makes sure the connection was closed
	// - twirc_free(), which frees the libtwirc state, so we don't leak
	twirc_kill(s);

	// That's all, wave good-bye!
	return EXIT_SUCCESS;
}

