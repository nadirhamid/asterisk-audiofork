/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2019, Nadir Hamid
 * Copyright (C) 2005 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Kevin P. Fleming <kpfleming@digium.com>
 *
 * Based on app_muxmon.c provided by
 * Anthony Minessale II <anthmct@yahoo.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief AudioFork() - Offload Asterisk audio processing to a Websocket server.
 * \ingroup applications
 *
 * \author Nadir Hamid <matrix.nad@gmail.com>
 *
 * \note Based on app_mixmonitor.c provided by
 * asterisk
 */

/*** MODULEINFO
	<use type="module">func_periodic_hook</use>
	<support_level>core</support_level>
 ***/

#ifndef AST_MODULE
#define AST_MODULE "Audiofork"
#endif


#include "asterisk.h"

#include "asterisk/paths.h"     /* use ast_config_AST_MONITOR_DIR */
#include "asterisk/stringfields.h"
#include "asterisk/file.h"
#include "asterisk/audiohook.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/cli.h"
#include "asterisk/app.h"
#include "asterisk/channel.h"
#include "asterisk/autochan.h"
#include "asterisk/manager.h"
#include "asterisk/callerid.h"
#include "asterisk/mod_format.h"
#include "asterisk/linkedlists.h"
#include "asterisk/test.h"
#include "asterisk/format_cache.h"
#include "asterisk/beep.h"

#include "asterisk/module.h"
#include "asterisk/astobj2.h"
#include "asterisk/pbx.h"
#include "asterisk/http_websocket.h"
#include "asterisk/tcptls.h"
#include "asterisk/strings.h"
#include <dlfcn.h>      /* dlsym() for optional header-capable websocket constructor */
#include <time.h>       /* struct timespec / nanosleep() */


/*** DOCUMENTATION
	<application name="AudioFork" language="en_US">
		<synopsis>
			Forks a raw audio stream to a websocket server.
		</synopsis>
		<syntax>
			<parameter name="wsserver" required="true" argsep=".">
				<argument name="wsserver" required="true">
					<para>the URL to the  websocket server you want to send the audio to. </para>
				</argument>
				<argument name="extension" required="true" />
			</parameter>
			<parameter name="options">
				<optionlist>
					<option name="b">
						<para>Only save audio to the file while the channel is bridged.</para>
						<note><para>If you utilize this option inside a Local channel, you must make sure the Local
						channel is not optimized away. To do this, be sure to call your Local channel with the
						<literal>/n</literal> option. For example: Dial(Local/start@mycontext/n)</para></note>
					</option>
					<option name="B">
						<para>Play a periodic beep while this call is being recorded.</para>
						<argument name="interval"><para>Interval, in seconds. Default is 15.</para></argument>
					</option>
					<option name="v">
						<para>Adjust the <emphasis>heard</emphasis> volume by a factor of <replaceable>x</replaceable>
						(range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="V">
						<para>Adjust the <emphasis>spoken</emphasis> volume by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="W">
						<para>Adjust both, <emphasis>heard and spoken</emphasis> volumes by a factor
						of <replaceable>x</replaceable> (range <literal>-4</literal> to <literal>4</literal>)</para>
						<argument name="x" required="true" />
					</option>
					<option name="r">
						<argument name="file" required="true" />
						<para>Use the specified file to record the <emphasis>receive</emphasis> audio feed.
						Like with the basic filename argument, if an absolute path isn't given, it will create
						the file in the configured monitoring directory.</para>
					</option>
					<option name="t">
						<argument name="file" required="true" />
						<para>Use the specified file to record the <emphasis>transmit</emphasis> audio feed.
						Like with the basic filename argument, if an absolute path isn't given, it will create
						the file in the configured monitoring directory.</para>
					</option>
					<option name="S">
						<para>When combined with the <replaceable>r</replaceable> or <replaceable>t</replaceable>
						option, inserts silence when necessary to maintain synchronization between the receive
						and transmit audio streams.</para>
					</option>
					<option name="i">
						<argument name="chanvar" required="true" />
						<para>Stores the AudioFork's ID on this channel variable.</para>
					</option>
					<option name="p">
						<para>Play a beep on the channel that starts the recording.</para>
					</option>
					<option name="P">
						<para>Play a beep on the channel that stops the recording.</para>
					</option>
					<option name="D">
						<para>Direction of audiohook to process - supports in, out, and both</para>
					</option>
					<option name="T">
						<para>comma separated TLS config for secure websocket connections</para>
					</option>
					<option name="R">
						<para>Timeout for reconnections</para>
					</option>
					<option name="r">
						<para>Number of times to attempt reconnect before closing connections</para>
					</option>
					<option name="c">
						<argument name="codec" required="true" />
						<para>Audio codec used when sending audio over the websocket. One of: SLIN, G722, PCMU, PCMA. For SLIN the sample rate option (L) selects the rate. For G722/PCMU/PCMA the rate is fixed at 8 kHz. The channel audio is transparently transcoded into this format before transmission.</para>
					</option>
					<option name="L">
						<argument name="rate" required="true" />
						<para>Sample rate (Hz) for SLIN audio: 8000, 16000, 32000 or 48000. Ignored for non-SLIN codecs.</para>
					</option>
					<option name="s">
						<argument name="subprotocol" required="true" />
						<para>WebSocket subprotocol sent during the handshake (e.g. audio/raw).</para>
					</option>
					<option name="H">
						<argument name="headers" required="true" />
						<para>Comma separated list of custom HTTP headers sent during the websocket handshake, as key:value pairs (e.g. X-Api-Key:abc123,Authorization:Bearer xyz).</para>
					</option>
					<option name="A">
						<argument name="token" required="true" />
						<para>Bearer token sent as an <literal>Authorization: Bearer &lt;token&gt;</literal> header during the websocket handshake.</para>
					</option>
					<option name="M">
						<para>Send a JSON metadata object (channel, caller ID number/name, dialed number, start time, selected sample rate and codec) over the websocket as a TEXT frame before the audio stream begins.</para>
					</option>
				</optionlist>
			</parameter>
			<parameter name="command">
				<para>This is executed when the audio fork's hook finishes</para>
				<para>Any strings matching <literal>^{X}</literal> will be unescaped to <variable>X</variable>.</para>
				<para>All variables will be evaluated at the time AudioFork is called.</para>
				<warning><para>Do not use untrusted strings such as <variable>CALLERID(num)</variable>
				or <variable>CALLERID(name)</variable> as part of the command parameters.  You
				risk a command injection attack executing arbitrary commands if the untrusted
				strings aren't filtered to remove dangerous characters.  See function
				<variable>FILTER()</variable>.</para></warning>
			</parameter>
		</syntax>
		<description>
			<para>Forks raw audio to a remote websocket.</para>
			<para>This application does not automatically answer and should be preceeded by
			an application such as Answer or Progress().</para>
			<note><para>AudioFork runs as an audiohook.</para></note>
			<variablelist>
				<variable name="AUDIOFORK_WSSERVER">
					<para>The URL of the websocket server.</para>
				</variable>
			</variablelist>
			<warning><para>Do not use untrusted strings such as <variable>CALLERID(num)</variable>
			or <variable>CALLERID(name)</variable> as part of ANY of the application's
			parameters.  You risk a command injection attack executing arbitrary commands
			if the untrusted strings aren't filtered to remove dangerous characters.  See
			function <variable>FILTER()</variable>.</para></warning>
		</description>
		<see-also>
			<ref type="application">AudioFork</ref>
			<ref type="application">StopAudioFork</ref>
			<ref type="application">PauseMonitor</ref>
			<ref type="application">UnpauseMonitor</ref>
			<ref type="function">AUDIOHOOK_INHERIT</ref>
		</see-also>
	</application>
	<application name="StopAudioFork" language="en_US">
		<synopsis>
			Cancels an ongoing audio fork and closes the websocket connection.
		</synopsis>
		<syntax>
			<parameter name="AudioForkID" required="false">
				<para>If a valid ID is provided, then this command will stop only that specific
				AudioFork.</para>
			</parameter>
		</syntax>
		<description>
			<para>Stop an ongoing AudioFork created previously by <literal>AudioFork()</literal>
			on the current channel.</para>
		</description>
		<see-also>
			<ref type="application">AudioFork</ref>
		</see-also>
	</application>
	<manager name="AudioForkMute" language="en_US">
		<synopsis>
			Mute / unMute a AudioFork session.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to mute.</para>
			</parameter>
			<parameter name="Direction">
				<para>Which part of the audio fork to mute:  read, write or both (from channel, to channel or both channels).</para>
			</parameter>
			<parameter name="State">
				<para>Turn mute on or off : 1 to turn on, 0 to turn off.</para>
			</parameter>
		</syntax>
		<description>
			<para>This action may be used to mute a AudioFork session.</para>
		</description>
	</manager>
	<manager name="AudioFork" language="en_US">
		<synopsis>
			Forks a raw audio stream to a websocket server.
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>Used to specify the channel to record.</para>
			</parameter>
			<parameter name="WsServer">
				<para>The websocket server URL to fork audio to.</para>
			</parameter>
			<parameter name="options">
				<para>Options that apply to the AudioFork in the same way as they
				would apply if invoked from the AudioFork application. For a list of
				available options, see the documentation for the audiofork application. </para>
			</parameter>
			<parameter name="Command">
				<para>Will be executed when the audio fork has completed.
				Any strings matching <literal>^{X}</literal> will be unescaped to <variable>X</variable>.
				All variables will be evaluated at the time AudioFork is called.</para>
				<warning><para>Do not use untrusted strings such as <variable>CALLERID(num)</variable>
				or <variable>CALLERID(name)</variable> as part of the command parameters.  You
				risk a command injection attack executing arbitrary commands if the untrusted
				strings aren't filtered to remove dangerous characters.  See function
				<variable>FILTER()</variable>.</para></warning>
			</parameter>
		</syntax>
		<description>
			<para>This action will fork audio from an ongoing call to the designated websocket serrver.</para>
			<variablelist>
				<variable name="AUDIOFORK_WSSERVER">
					<para>The websocket server URL.</para>
				</variable>
			</variablelist>
		</description>
	</manager>
	<manager name="StopAudioFork" language="en_US">
		<synopsis>
			Stops an ongoing AudioFork() session
		</synopsis>
		<syntax>
			<xi:include xpointer="xpointer(/docs/manager[@name='Login']/syntax/parameter[@name='ActionID'])" />
			<parameter name="Channel" required="true">
				<para>The name of the channel monitored.</para>
			</parameter>
			<parameter name="AudioForkID" required="false">
				<para>If a valid ID is provided, then this command will stop only that specific
				AudioFork.</para>
			</parameter>
		</syntax>
		<description>
			<para>This command stops the audio fork that was created by the <literal>AudioFork</literal>
			action.</para>
		</description>
	</manager>
	<function name="AUDIOFORK" language="en_US">
		<synopsis>
			Retrieve data pertaining to specific instances of AudioFork on a channel.
		</synopsis>
		<syntax>
			<parameter name="id" required="true">
				<para>The unique ID of the AudioFork instance. The unique ID can be retrieved through the channel
				variable used as an argument to the <replaceable>i</replaceable> option to AudioFork.</para>
			</parameter>
			<parameter name="key" required="true">
				<para>The piece of data to retrieve from the AudioFork.</para>
				<enumlist>
					<enum name="filename" />
				</enumlist>
			</parameter>
		</syntax>
	</function>

 ***/

#define SAMPLES_PER_FRAME 160
#define get_volfactor(x) x ? ((x > 0) ? (1 << x) : ((1 << abs(x)) * -1)) : 0

/*! \brief Reconnection exponential backoff boundaries (in seconds) */
#define RECONNECT_MIN_DELAY 1
#define RECONNECT_MAX_DELAY 30

/*! \brief Default audio codec when none is specified */
#define AUDIOFORK_DEFAULT_CODEC "SLIN"
#define AUDIOFORK_DEFAULT_SUBPROTOCOL "echo"

static const char *const app = "AudioFork";

static const char *const stop_app = "StopAudioFork";

static const char *const audiofork_spy_type = "AudioFork";

struct audiofork {
	struct ast_audiohook audiohook;
	struct ast_websocket *websocket;
	char *wsserver;
	struct ast_tls_config *tls_cfg;
	char *tcert;
	enum ast_audiohook_direction direction;
	const char *direction_string;
	int reconnection_attempts;
	int reconnection_timeout;
	char *post_process;
	char *name;
	ast_callid callid;
	unsigned int flags;
	struct ast_autochan *autochan;
	struct audiofork_ds *audiofork_ds;

	/* Configurable audio format / websocket handshake parameters */
	char *codec;                 /*!< requested codec name (SLIN, G722, PCMU, PCMA) */
	int sample_rate;             /*!< requested sample rate in Hz (SLIN only) */
	char *subprotocol;           /*!< websocket subprotocol sent during handshake */
	char *headers;               /*!< custom HTTP headers "k:v,k:v" sent during handshake */
	unsigned int send_metadata;  /*!< send JSON metadata before the audio stream */
	struct ast_format *format;   /*!< resolved ast_format used for audiohook reads */

	/* the below string fields describe data used for creating voicemails from the recording */
	 AST_DECLARE_STRING_FIELDS(
		AST_STRING_FIELD(call_context);
		AST_STRING_FIELD(call_macrocontext);
		AST_STRING_FIELD(call_extension);
		AST_STRING_FIELD(call_callerchan);
		AST_STRING_FIELD(call_callerid);
	);
	int call_priority;
	int has_tls;
};

enum audiofork_flags {
	MUXFLAG_APPEND = (1 << 1),
	MUXFLAG_BRIDGED = (1 << 2),
	MUXFLAG_VOLUME = (1 << 3),
	MUXFLAG_READVOLUME = (1 << 4),
	MUXFLAG_WRITEVOLUME = (1 << 5),
	MUXFLAG_COMBINED = (1 << 8),
	MUXFLAG_UID = (1 << 9),
	MUXFLAG_BEEP = (1 << 11),
	MUXFLAG_BEEP_START = (1 << 12),
	MUXFLAG_BEEP_STOP = (1 << 13),
	MUXFLAG_RWSYNC = (1 << 14),
	MUXFLAG_DIRECTION = (1 << 15),
	MUXFLAG_TLS = (1 << 16),
	MUXFLAG_RECONNECTION_TIMEOUT = (1 << 17),
	MUXFLAG_RECONNECTION_ATTEMPTS = (1 << 18),
	MUXFLAG_SAMPLE_RATE = (1 << 19),
	MUXFLAG_CODEC = (1 << 20),
	MUXFLAG_SUBPROTOCOL = (1 << 21),
	MUXFLAG_HEADERS = (1 << 22),
	MUXFLAG_AUTH = (1 << 23),
	MUXFLAG_METADATA = (1 << 24),
};

enum audiofork_args {
	OPT_ARG_READVOLUME = 0,
	OPT_ARG_WRITEVOLUME,
	OPT_ARG_VOLUME,
	OPT_ARG_UID,
	OPT_ARG_BEEP_INTERVAL,
	OPT_ARG_RWSYNC,
	OPT_ARG_DIRECTION,
	OPT_ARG_TLS,
	OPT_ARG_RECONNECTION_TIMEOUT,
	OPT_ARG_RECONNECTION_ATTEMPTS,
	OPT_ARG_CODEC,
	OPT_ARG_SAMPLE_RATE,
	OPT_ARG_SUBPROTOCOL,
	OPT_ARG_HEADERS,
	OPT_ARG_AUTH,
	OPT_ARG_ARRAY_SIZE,           /* Always last element of the enum */
};

AST_APP_OPTIONS(audiofork_opts, {
	AST_APP_OPTION('a', MUXFLAG_APPEND),
	AST_APP_OPTION('b', MUXFLAG_BRIDGED),
	AST_APP_OPTION_ARG('B', MUXFLAG_BEEP, OPT_ARG_BEEP_INTERVAL),
	AST_APP_OPTION('p', MUXFLAG_BEEP_START),
	AST_APP_OPTION('P', MUXFLAG_BEEP_STOP),
	AST_APP_OPTION_ARG('v', MUXFLAG_READVOLUME, OPT_ARG_READVOLUME),
	AST_APP_OPTION_ARG('V', MUXFLAG_WRITEVOLUME, OPT_ARG_WRITEVOLUME), 
	AST_APP_OPTION_ARG('W', MUXFLAG_VOLUME, OPT_ARG_VOLUME),
	AST_APP_OPTION_ARG('i', MUXFLAG_UID, OPT_ARG_UID),
	AST_APP_OPTION_ARG('S', MUXFLAG_RWSYNC, OPT_ARG_RWSYNC),
	AST_APP_OPTION_ARG('D', MUXFLAG_DIRECTION, OPT_ARG_DIRECTION),
	AST_APP_OPTION_ARG('T', MUXFLAG_TLS, OPT_ARG_TLS),
	AST_APP_OPTION_ARG('R', MUXFLAG_RECONNECTION_TIMEOUT, OPT_ARG_RECONNECTION_TIMEOUT),
	AST_APP_OPTION_ARG('r', MUXFLAG_RECONNECTION_ATTEMPTS, OPT_ARG_RECONNECTION_ATTEMPTS),
	AST_APP_OPTION_ARG('c', MUXFLAG_CODEC, OPT_ARG_CODEC),
	AST_APP_OPTION_ARG('L', MUXFLAG_SAMPLE_RATE, OPT_ARG_SAMPLE_RATE),
	AST_APP_OPTION_ARG('s', MUXFLAG_SUBPROTOCOL, OPT_ARG_SUBPROTOCOL),
	AST_APP_OPTION_ARG('H', MUXFLAG_HEADERS, OPT_ARG_HEADERS),
	AST_APP_OPTION_ARG('A', MUXFLAG_AUTH, OPT_ARG_AUTH),
	AST_APP_OPTION('M', MUXFLAG_METADATA),
});

struct audiofork_ds {
	unsigned int destruction_ok;
	ast_cond_t destruction_condition;
	ast_mutex_t lock;
	/**
	 * the audio hook we will use for sending raw audio
	 */
	struct ast_audiohook *audiohook;

	unsigned int samp_rate;
	char *wsserver;
	char *beep_id;
	struct ast_tls_config *tls_cfg;

	/* Configurable audio format / websocket handshake parameters mirrored for the datastore */
	char *codec;
	char *subprotocol;
	char *headers;
	unsigned int send_metadata;
	struct ast_format *format;
};

static void audiofork_ds_destroy(void *data)
{
	struct audiofork_ds *audiofork_ds = data;

	ast_mutex_lock(&audiofork_ds->lock);
	audiofork_ds->audiohook = NULL;
	audiofork_ds->destruction_ok = 1;
	ast_free(audiofork_ds->wsserver);
	ast_free(audiofork_ds->beep_id);
	ast_free(audiofork_ds->codec);
	ast_free(audiofork_ds->subprotocol);
	ast_free(audiofork_ds->headers);
	if (audiofork_ds->format) {
		ao2_cleanup(audiofork_ds->format);
	}
	ast_cond_signal(&audiofork_ds->destruction_condition);
	ast_mutex_unlock(&audiofork_ds->lock);
}

static const struct ast_datastore_info audiofork_ds_info = {
	.type = "audiofork",
	.destroy = audiofork_ds_destroy,
};

static void destroy_monitor_audiohook(struct audiofork *audiofork)
{
	if (audiofork->audiofork_ds) {
		ast_mutex_lock(&audiofork->audiofork_ds->lock);
		audiofork->audiofork_ds->audiohook = NULL;
		ast_mutex_unlock(&audiofork->audiofork_ds->lock);
	}
	/* kill the audiohook. */
	ast_audiohook_lock(&audiofork->audiohook);
	ast_audiohook_detach(&audiofork->audiohook);
	ast_audiohook_unlock(&audiofork->audiohook);
	ast_audiohook_destroy(&audiofork->audiohook);
}

static int start_audiofork(struct ast_channel *chan, struct ast_audiohook *audiohook)
{
	if (!chan) {
		return -1;
	}

	return ast_audiohook_attach(chan, audiohook);
}

static int audiofork_ws_close(struct audiofork *audiofork)
{
	int ret;
	ast_verb(2, "[AudioFork] Closing websocket connection\n");
	if (audiofork->websocket) {
		ast_verb(2, "[AudioFork] Calling ast_websocket_close\n");
		ret = ast_websocket_close(audiofork->websocket, 1011);
		return ret;
	}

	ast_verb(2, "[AudioFork] No reference to websocket, can't close connection\n");
	return -1;
}


/*
 * Remove control characters (everything below 0x20 except a horizontal tab)
 * from a string in place. Used to sanitise custom header keys/values and to
 * prevent header-injection attacks (e.g. embedded CR/LF).
 */
static void audiofork_strip_control(char *s)
{
	char *r, *w;

	if (!s) {
		return;
	}
	for (r = w = s; *r; r++) {
		if ((unsigned char) *r < 0x20 && *r != '\t') {
			continue;
		}
		*w++ = *r;
	}
	*w = '\0';
}

/*
 * Validate, sanitise and normalise a raw header string of the form
 * "Key:Value,Key:Value". Invalid pairs (missing ':' or empty key/value) are
 * skipped with a warning; control characters are stripped. The cleaned,
 * comma-separated string is written to \a out (bounded by \a outlen).
 */
static void audiofork_normalize_headers(const char *raw, char *out, size_t outlen)
{
	char *copy, *pair, *save = NULL;
	size_t len = 0;

	out[0] = '\0';
	if (ast_strlen_zero(raw) || outlen < 2) {
		return;
	}

	copy = ast_strdupa(raw);
	for (pair = strtok_r(copy, ",", &save); pair; pair = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(pair, ':');
		int written;

		if (!colon) {
			ast_log(LOG_WARNING, "[AudioFork] Skipping malformed header (missing ':'): '%s'\n", pair);
			continue;
		}
		*colon = '\0';
		ast_trim_blanks(pair);
		ast_trim_blanks(colon + 1);
		audiofork_strip_control(pair);
		audiofork_strip_control(colon + 1);

		if (ast_strlen_zero(pair) || ast_strlen_zero(colon + 1)) {
			ast_log(LOG_WARNING, "[AudioFork] Skipping header with empty key or value\n");
			continue;
		}

		written = snprintf(out + len, outlen - len, "%s%s:%s", len ? "," : "", pair, colon + 1);
		if (written < 0 || (size_t) written >= outlen - len) {
			ast_log(LOG_WARNING, "[AudioFork] Combined headers string truncated; some headers dropped\n");
			break;
		}
		len += (size_t) written;
	}
}

/*
 * Build an ast_variable list from a (already sanitised) "Key:Value,..." string.
 * The caller is responsible for freeing it with ast_variables_destroy().
 */
static struct ast_variable *audiofork_parse_headers_to_list(const char *header_str)
{
	struct ast_variable *head = NULL;
	char *copy, *pair, *save = NULL;

	if (ast_strlen_zero(header_str)) {
		return NULL;
	}

	copy = ast_strdupa(header_str);
	for (pair = strtok_r(copy, ",", &save); pair; pair = strtok_r(NULL, ",", &save)) {
		char *colon = strchr(pair, ':');
		if (!colon) {
			continue; /* already validated/sanitised earlier */
		}
		*colon = '\0';
		ast_trim_blanks(pair);
		ast_trim_blanks(colon + 1);
		ast_variable_list_append(&head, ast_variable_new(pair, colon + 1, ""));
	}
	return head;
}

/*
 * Optional, header-capable WebSocket client constructor. It is NOT present in
 * all Asterisk builds, so it is resolved at runtime via dlsym(); this keeps the
 * module linking and loading on stock Asterisk. When present it lets us send
 * the custom headers / Bearer token during the handshake.
 */
typedef struct ast_websocket *(*audiofork_ws_create_with_headers_fn)(
	const char *uri, const char *protocol, struct ast_variable *headers,
	struct ast_tls_config *tls_cfg, enum ast_websocket_result *result);

static audiofork_ws_create_with_headers_fn audiofork_get_ws_create_with_headers(void)
{
	static int resolved = 0;
	static audiofork_ws_create_with_headers_fn fn = NULL;

	if (!resolved) {
		fn = (audiofork_ws_create_with_headers_fn) dlsym(RTLD_DEFAULT, "ast_websocket_client_create_with_headers");
		resolved = 1;
	}
	return fn;
}

/*
	1 = success
	0 = fail
*/
static enum ast_websocket_result audiofork_ws_connect(struct audiofork *audiofork)
{
	enum ast_websocket_result result;

	if (audiofork->websocket) {
		ast_verb(2, "<%s> [AudioFork] (%s) Reconnecting to websocket server at: %s\n",
			ast_channel_name(audiofork->autochan->chan),
			audiofork->direction_string,
			audiofork->audiofork_ds->wsserver);

		// close the websocket connection before reconnecting
		audiofork_ws_close(audiofork);

		ao2_cleanup(audiofork->websocket);
	}
	else {
		ast_verb(2, "<%s> [AudioFork] (%s) Connecting to websocket server at: %s\n",
			ast_channel_name(audiofork->autochan->chan),
			audiofork->direction_string,
			audiofork->audiofork_ds->wsserver);
	}

	// Check if we're running with TLS
	{
		const char *subprotocol = S_OR(audiofork->subprotocol, AUDIOFORK_DEFAULT_SUBPROTOCOL);
		struct ast_variable *extra_headers = NULL;
		audiofork_ws_create_with_headers_fn create_with_headers = audiofork_get_ws_create_with_headers();

		if (!ast_strlen_zero(audiofork->headers)) {
			extra_headers = audiofork_parse_headers_to_list(audiofork->headers);
			ast_verb(2, "<%s> [AudioFork] (%s) Custom handshake headers: %s\n",
				ast_channel_name(audiofork->autochan->chan), audiofork->direction_string,
				audiofork->headers);
		}

		/* Prefer the header-capable constructor when it exists and we actually
		 * have headers to send (e.g. custom headers and/or a Bearer token). */
		if (create_with_headers && extra_headers) {
			ast_verb(2, "<%s> [AudioFork] (%s) Using ast_websocket_client_create_with_headers() (custom headers supported)\n",
				ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
			if (audiofork->has_tls == 1) {
				audiofork->websocket = create_with_headers(audiofork->audiofork_ds->wsserver,
					subprotocol, extra_headers, audiofork->tls_cfg, &result);
			} else {
				audiofork->websocket = create_with_headers(audiofork->audiofork_ds->wsserver,
					subprotocol, extra_headers, NULL, &result);
			}
		} else {
			if (extra_headers) {
				/* The stock ast_websocket_client_create() API only accepts a
				 * subprotocol and not arbitrary headers, so they cannot be sent.
				 * Warn clearly; the server must accept credentials via the
				 * subprotocol or a URL query string instead. */
				ast_log(LOG_WARNING, "<%s> [AudioFork] (%s) ast_websocket_client_create_with_headers() not available in this Asterisk build; custom headers / Bearer token will NOT be transmitted. Supply credentials via the subprotocol or URL query string.\n",
					ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
			}
			if (audiofork->has_tls == 1) {
				ast_verb(2, "<%s> [AudioFork] (%s) Creating WebSocket server with TLS mode enabled (subprotocol: %s)\n",
					ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, subprotocol);
				audiofork->websocket = ast_websocket_client_create(audiofork->audiofork_ds->wsserver, subprotocol, audiofork->tls_cfg, &result);
			} else {
				ast_verb(2, "<%s> [AudioFork] (%s) Creating WebSocket server without TLS (subprotocol: %s)\n",
					ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, subprotocol);
				audiofork->websocket = ast_websocket_client_create(audiofork->audiofork_ds->wsserver, subprotocol, NULL, &result);
			}
		}

		if (extra_headers) {
			ast_variables_destroy(extra_headers);
		}
	}

	return result;

/*
 * Sleep (interruptibly) for up to \a seconds seconds, waking early (returning
 * 1) if the audiofork has been told to stop running in the meantime. Uses a
 * 100 ms granularity via nanosleep() so a StopAudioFork is honoured quickly
 * instead of waiting up to a whole second.
 */
static int audiofork_sleep_interruptible(int seconds, struct audiofork *audiofork)
{
	struct timespec ts;
	int elapsed = 0;

	ts.tv_sec = 0;
	ts.tv_nsec = 100000000L; /* 100 milliseconds */

	while (elapsed < seconds) {
		if (audiofork->audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
			return 1;
		}
		nanosleep(&ts, NULL);
		elapsed++;
	}

	return 0;
}

/*
	reconn_status
	0 = OK
	1 = FAILED
*/
static int audiofork_start_reconnecting(struct audiofork *audiofork)
{
	int attempts = audiofork->reconnection_attempts;
	int delay = audiofork->reconnection_timeout > 0 ? audiofork->reconnection_timeout : RECONNECT_MIN_DELAY;
	int attempt;
	enum ast_websocket_result result;

	if (attempts <= 0) {
		ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) No reconnection attempts configured; giving up.\n",
			ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
		return 1;
	}

	for (attempt = 1; attempt <= attempts; attempt++) {
		ast_log(LOG_WARNING, "<%s> [AudioFork] (%s) Reconnection attempt %d/%d in %d second(s)...\n",
			ast_channel_name(audiofork->autochan->chan), audiofork->direction_string,
			attempt, attempts, delay);

		/* Wait before retrying, but bail out if the hook has been stopped */
		if (audiofork_sleep_interruptible(delay, audiofork)) {
			ast_log(LOG_NOTICE, "<%s> [AudioFork] (%s) Reconnection cancelled.\n",
				ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
			return 1;
		}

		result = audiofork_ws_connect(audiofork);
		if (result == WS_OK) {
			ast_log(LOG_NOTICE, "<%s> [AudioFork] (%s) Successfully reconnected on attempt %d/%d (delay was %d s).\n",
				ast_channel_name(audiofork->autochan->chan), audiofork->direction_string,
				attempt, attempts, delay);
			return 0;
		}

		ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) Reconnection attempt %d/%d failed (result %d).\n",
			ast_channel_name(audiofork->autochan->chan), audiofork->direction_string,
			attempt, attempts, result);

		/* exponential backoff: double the delay, capped at the maximum */
		delay <<= 1;
		if (delay > RECONNECT_MAX_DELAY) {
			delay = RECONNECT_MAX_DELAY;
		}
	}

	ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) Exhausted %d reconnection attempts; giving up.\n",
		ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, attempts);

	return 1;
}

static void audiofork_free(struct audiofork *audiofork)
{
	if (audiofork) {
		if (audiofork->audiofork_ds) {
			ast_mutex_destroy(&audiofork->audiofork_ds->lock);
			ast_cond_destroy(&audiofork->audiofork_ds->destruction_condition);
			ast_free(audiofork->audiofork_ds);
			audiofork->audiofork_ds = NULL;
		}

		ast_free(audiofork->name);
		ast_free(audiofork->post_process);
		ast_free(audiofork->wsserver);
		ast_free(audiofork->codec);
		ast_free(audiofork->subprotocol);
		ast_free(audiofork->headers);
		if (audiofork->format) {
			ao2_cleanup(audiofork->format);
		}

		audiofork_ws_close(audiofork);

		/* clean stringfields */
		ast_string_field_free_memory(audiofork);

		ast_free(audiofork);
	}
}





/*!
 * \brief Resolve the ast_format to request from the audiohook.
 *
 * For SLIN the caller supplied \a rate is honoured (8000/16000/32000/48000).
 * For G722/PCMU/PCMA the rate is forced to 8000 Hz (their native clock rate).
 * \a rate is updated in place to reflect the rate actually used.
 * \return ao2 bumped ast_format (caller must ao2_cleanup) or NULL on failure.
 */
static struct ast_format *audiofork_get_format(const char *codec, int *rate)
{
	if (ast_strlen_zero(codec) || !strcasecmp(codec, "SLIN")) {
		switch (*rate) {
		case 8000:
		case 16000:
		case 32000:
		case 48000:
			break;
		default:
			ast_log(LOG_WARNING, "[AudioFork] Unsupported SLIN sample rate %d, defaulting to 8000\n", *rate);
			*rate = 8000;
			break;
		}
		return ast_format_cache_get_slin_by_rate(*rate);
	} else if (!strcasecmp(codec, "G722")) {
		*rate = 8000;
		return ao2_bump(ast_format_g722);
	} else if (!strcasecmp(codec, "PCMU")) {
		*rate = 8000;
		return ao2_bump(ast_format_ulaw);
	} else if (!strcasecmp(codec, "PCMA")) {
		*rate = 8000;
		return ao2_bump(ast_format_alaw);
	}

	ast_log(LOG_WARNING, "[AudioFork] Unknown codec '%s', defaulting to SLIN/8000\n", codec);
	*rate = 8000;
	return ast_format_cache_get_slin_by_rate(8000);
}

/*! \brief Append a JSON-escaped string to the dynamic buffer. */
static void json_append_string(struct ast_str **buf, const char *s)
{
	const char *p;

	ast_str_append(buf, 0, "\"");
	for (p = s; p && *p; p++) {
		switch (*p) {
		case '"':
			ast_str_append(buf, 0, "\\\"");
			break;
		case '\\':
			ast_str_append(buf, 0, "\\\\");
			break;
		case '\n':
			ast_str_append(buf, 0, "\\n");
			break;
		case '\r':
			ast_str_append(buf, 0, "\\r");
			break;
		case '\t':
			ast_str_append(buf, 0, "\\t");
			break;
		case '\b':
			ast_str_append(buf, 0, "\\b");
			break;
		case '\f':
			ast_str_append(buf, 0, "\\f");
			break;
		default:
			if ((unsigned char) *p < 0x20) {
				ast_str_append(buf, 0, "\\u%04x", (unsigned char) *p);
			} else {
				ast_str_append(buf, 0, "%c", *p);
			}
			break;
		}
	}
	ast_str_append(buf, 0, "\"");
}

/*!
 * \brief Build the JSON metadata object describing the call.
 * \return Newly allocated string (caller must ast_free) or NULL on failure.
 */
static char *audiofork_build_metadata(struct audiofork *audiofork)
{
	struct ast_channel *chan;
	const struct ast_party_caller *caller;
	const char *cid_num = "";
	const char *cid_name = "";
	const char *exten;
	const char *chan_name;
	time_t start;
	struct ast_str *buf;
	char *out;

	if (!audiofork->autochan || !audiofork->autochan->chan) {
		return NULL;
	}

	chan = audiofork->autochan->chan;
	caller = ast_channel_caller(chan);
	if (caller) {
		cid_num = S_OR(caller->id.number.str, "");
		cid_name = S_OR(caller->id.name.str, "");
	}
	exten = ast_channel_exten(chan);
	chan_name = ast_channel_name(chan);
	start = time(NULL);

	if (!(buf = ast_str_create(256))) {
		return NULL;
	}

	ast_str_append(&buf, 0, "{");
	ast_str_append(&buf, 0, "\"channel\":");
	json_append_string(buf, chan_name);
	ast_str_append(&buf, 0, ",\"caller_id_number\":");
	json_append_string(buf, cid_num);
	ast_str_append(&buf, 0, ",\"caller_id_name\":");
	json_append_string(buf, cid_name);
	ast_str_append(&buf, 0, ",\"dialed_number\":");
	json_append_string(buf, exten);
	ast_str_append(&buf, 0, ",\"start_time\":%ld", (long) start);
	ast_str_append(&buf, 0, ",\"sample_rate\":%d", audiofork->sample_rate);
	ast_str_append(&buf, 0, ",\"codec\":");
	json_append_string(buf, S_OR(audiofork->codec, AUDIOFORK_DEFAULT_CODEC));
	ast_str_append(&buf, 0, "}");

	out = ast_strdup(ast_str_buffer(buf));
	ast_free(buf);

	return out;
}

static void *audiofork_thread(void *obj)
{
	struct audiofork *audiofork = obj;
	struct ast_format *format;
	char *channel_name_cleanup;
	enum ast_websocket_result result;
	int frames_sent = 0;
	int reconn_status;

	/* Keep callid association before any log messages */
	if (audiofork->callid) {
		ast_verb(2, "<%s> [AudioFork] (%s) Keeping Call-ID Association\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
		ast_callid_threadassoc_add(audiofork->callid);
	}

	result = audiofork_ws_connect(audiofork);
	if (result != WS_OK) {
		ast_log(LOG_ERROR, "<%s> Could not connect to websocket server: %s\n", ast_channel_name(audiofork->autochan->chan), audiofork->audiofork_ds->wsserver);

		ast_test_suite_event_notify("AUDIOFORK_END", "Ws server: %s\r\n", audiofork->wsserver);

		/* kill the audiohook */
		destroy_monitor_audiohook(audiofork);
		ast_autochan_destroy(audiofork->autochan);

		/* We specifically don't do audiofork_free(audiofork) here because the automatic datastore cleanup will get it */

		ast_module_unref(ast_module_info->self);

		return 0;
	}

	ast_verb(2, "<%s> [AudioFork] (%s) Begin AudioFork Recording %s\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, audiofork->name);

	/* Determine the format the audiohook must deliver. ast_audiohook_read_frame()
	 * transparently transcodes the channel audio into the requested format. */
	format = audiofork->format;
	if (!format) {
		ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) No audio format configured; aborting.\n",
			ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
		destroy_monitor_audiohook(audiofork);
		ast_autochan_destroy(audiofork->autochan);
		ast_module_unref(ast_module_info->self);
		return 0;
	}

	/* Send JSON metadata over the websocket before the audio stream, if requested.
	 * Retry a few times (with a short delay) to survive transient congestion; if
	 * all attempts fail we log an error but continue with the audio stream,
	 * because metadata is not critical for media delivery. */
	if (audiofork->send_metadata) {
		char *metadata = audiofork_build_metadata(audiofork);
		if (metadata) {
			int attempt;
			int meta_ok = 0;
			struct timespec meta_sleep = { 0, 200000000L }; /* 200 ms */

			for (attempt = 1; attempt <= 3 && !meta_ok; attempt++) {
				ast_verb(2, "<%s> [AudioFork] (%s) Sending metadata (attempt %d/3): %s\n",
					ast_channel_name(audiofork->autochan->chan), audiofork->direction_string,
					attempt, metadata);
				if (!ast_websocket_write(audiofork->websocket, AST_WEBSOCKET_OPCODE_TEXT, metadata, strlen(metadata))) {
					meta_ok = 1;
				} else {
					ast_log(LOG_WARNING, "<%s> [AudioFork] (%s) Metadata send attempt %d/3 failed\n",
						ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, attempt);
					if (attempt < 3) {
						nanosleep(&meta_sleep, NULL);
					}
				}
			}

			if (!meta_ok) {
				ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) All metadata send attempts failed; continuing with audio stream.\n",
					ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
			}
			ast_free(metadata);
		}
	}

	/* The audiohook must enter and exit the loop locked */
	ast_audiohook_lock(&audiofork->audiohook);

	while (audiofork->audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING) {
		// ast_verb(2, "<%s> [AudioFork] (%s) Reading Audio Hook frame...\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
		struct ast_frame *fr = ast_audiohook_read_frame(&audiofork->audiohook, SAMPLES_PER_FRAME, audiofork->direction, format);

		if (!fr) {
			ast_audiohook_trigger_wait(&audiofork->audiohook);

			if (audiofork->audiohook.status != AST_AUDIOHOOK_STATUS_RUNNING) {
				ast_verb(2, "<%s> [AudioFork] (%s) AST_AUDIOHOOK_STATUS_RUNNING = 0\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
				break;
			}

			continue;
		}

		/* audiohook lock is not required for the next block.
		 * Unlock it, but remember to lock it before looping or exiting */
		ast_audiohook_unlock(&audiofork->audiohook);
		struct ast_frame *cur;

		//ast_mutex_lock(&audiofork->audiofork_ds->lock);
		for (cur = fr; cur; cur = AST_LIST_NEXT(cur, frame_list)) {
			// ast_verb(2, "<%s> sending audio frame to websocket...\n", ast_channel_name(audiofork->autochan->chan));
			// ast_mutex_lock(&audiofork->audiofork_ds->lock);

			if (ast_websocket_write(audiofork->websocket, AST_WEBSOCKET_OPCODE_BINARY, cur->data.ptr, cur->datalen)) {

				ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) Could not write to websocket.  Reconnecting...\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
				reconn_status = audiofork_start_reconnecting(audiofork);

				if (reconn_status == 1) {
					audiofork->websocket = NULL;
					audiofork->audiohook.status = AST_AUDIOHOOK_STATUS_SHUTDOWN;
					break;
				}

				/* re-send the last frame */
				if (ast_websocket_write(audiofork->websocket, AST_WEBSOCKET_OPCODE_BINARY, cur->data.ptr, cur->datalen)) {
					ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) Could not re-write to websocket.  Complete Failure.\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);

					audiofork->audiohook.status = AST_AUDIOHOOK_STATUS_SHUTDOWN;
					break;
				}
			}

			frames_sent++;
		}

		//ast_mutex_unlock(&audiofork->audiofork_ds->lock);
		//

		/* All done! free it. */
		if (fr) {
			ast_frame_free(fr, 0);
		}

		fr = NULL;

		ast_audiohook_lock(&audiofork->audiohook);
	}

	ast_audiohook_unlock(&audiofork->audiohook);

	if (ast_test_flag(audiofork, MUXFLAG_BEEP_STOP)) {
		ast_autochan_channel_lock(audiofork->autochan);
		ast_stream_and_wait(audiofork->autochan->chan, "beep", "");
		ast_autochan_channel_unlock(audiofork->autochan);
	}

	channel_name_cleanup = ast_strdupa(ast_channel_name(audiofork->autochan->chan));

	ast_autochan_destroy(audiofork->autochan);

	/* Datastore cleanup: wait until the datastore has been removed by
	 * stop_audiofork_full() or channel teardown. audiofork_ds_destroy() has
	 * already released the ds's dynamically-allocated members and signalled
	 * this condition. The ds container itself is owned by this thread, so we
	 * release it here and clear the pointer; audiofork_free() keeps an
	 * if (audiofork->audiofork_ds) guard and will therefore NOT free it a
	 * second time (this prevents a double free / use-after-free). */
	{
		struct audiofork_ds *ds = audiofork->audiofork_ds;

		ast_mutex_lock(&ds->lock);
		if (!ds->destruction_ok) {
			ast_cond_wait(&ds->destruction_condition, &ds->lock);
		}
		ast_mutex_unlock(&ds->lock);

		audiofork->audiofork_ds = NULL;
		ast_mutex_destroy(&ds->lock);
		ast_cond_destroy(&ds->destruction_condition);
		ast_free(ds);
	}

	/* kill the audiohook */
	destroy_monitor_audiohook(audiofork);

	ast_verb(2, "<%s> [AudioFork] (%s) Finished processing audiohook. Frames sent = %d\n", channel_name_cleanup, audiofork->direction_string, frames_sent);
	ast_verb(2, "<%s> [AudioFork] (%s) Post Process\n", channel_name_cleanup, audiofork->direction_string);

	if (audiofork->post_process) {
		ast_verb(2, "<%s> [AudioFork] (%s) Executing [%s]\n", channel_name_cleanup, audiofork->direction_string, audiofork->post_process);
		ast_safe_system(audiofork->post_process);
	}

	// audiofork->name

	ast_verb(2, "<%s> [AudioFork] (%s) End AudioFork Recording to: %s\n", channel_name_cleanup, audiofork->direction_string, audiofork->wsserver);
	ast_test_suite_event_notify("AUDIOFORK_END", "Ws server: %s\r\n", audiofork->wsserver);

	/* free any audiofork memory */
	audiofork_free(audiofork);

	ast_module_unref(ast_module_info->self);

	return NULL;
}

static int setup_audiofork_ds(struct audiofork *audiofork, struct ast_channel *chan, char **datastore_id, const char *beep_id)
{
	struct ast_datastore *datastore = NULL;
	struct audiofork_ds *audiofork_ds;

	if (!(audiofork_ds = ast_calloc(1, sizeof(*audiofork_ds)))) {
		return -1;
	}

	if (ast_asprintf(datastore_id, "%p", audiofork_ds) == -1) {
		ast_log(LOG_ERROR, "Failed to allocate memory for AudioFork ID.\n");
		ast_free(audiofork_ds);
		return -1;
	}

	ast_mutex_init(&audiofork_ds->lock);
	ast_cond_init(&audiofork_ds->destruction_condition, NULL);

	if (!(datastore = ast_datastore_alloc(&audiofork_ds_info, *datastore_id))) {
		ast_mutex_destroy(&audiofork_ds->lock);
		ast_cond_destroy(&audiofork_ds->destruction_condition);
		ast_free(audiofork_ds);
		return -1;
	}

	if (ast_test_flag(audiofork, MUXFLAG_BEEP_START)) {
		ast_autochan_channel_lock(audiofork->autochan);
		ast_stream_and_wait(audiofork->autochan->chan, "beep", "");
		ast_autochan_channel_unlock(audiofork->autochan);
	}

	audiofork_ds->samp_rate = audiofork->sample_rate;
	audiofork_ds->codec = ast_strdup(S_OR(audiofork->codec, AUDIOFORK_DEFAULT_CODEC));
	audiofork_ds->subprotocol = ast_strdup(S_OR(audiofork->subprotocol, AUDIOFORK_DEFAULT_SUBPROTOCOL));
	audiofork_ds->headers = ast_strdup(audiofork->headers);
	audiofork_ds->send_metadata = audiofork->send_metadata;
	audiofork_ds->format = audiofork->format ? ao2_bump(audiofork->format) : NULL;
	audiofork_ds->audiohook = &audiofork->audiohook;
	audiofork_ds->wsserver = ast_strdup(audiofork->wsserver);
	if (!ast_strlen_zero(beep_id)) {
		audiofork_ds->beep_id = ast_strdup(beep_id);
	}
	datastore->data = audiofork_ds;

	ast_channel_lock(chan);
	ast_channel_datastore_add(chan, datastore);
	ast_channel_unlock(chan);

	audiofork->audiofork_ds = audiofork_ds;
	return 0;
}

static int launch_audiofork_thread(
	struct ast_channel *chan,
	const char *wsserver, unsigned int flags,
	enum ast_audiohook_direction direction,
	char* tcert,
	int reconn_timeout,
	int reconn_attempts,
	int readvol, int writevol,
	const char *post_process,
	const char *uid_channel_var,
	const char *beep_id,
	int sample_rate,
	const char *codec,
	const char *subprotocol,
	const char *headers,
	int send_metadata
)
{
	pthread_t thread;
	struct audiofork *audiofork;
	char postprocess2[1024] = "";
	char *datastore_id = NULL;

	postprocess2[0] = 0;
	/* If a post process system command is given attach it to the structure */
	if (!ast_strlen_zero(post_process)) {
		char *p1, *p2;

		p1 = ast_strdupa(post_process);
		for (p2 = p1; *p2; p2++) {
			if (*p2 == '^' && *(p2 + 1) == '{') {
				*p2 = '$';
			}
		}
		ast_channel_lock(chan);
		pbx_substitute_variables_helper(chan, p1, postprocess2, sizeof(postprocess2) - 1);
		ast_channel_unlock(chan);
	}

	/* Pre-allocate audiofork structure and spy */
	if (!(audiofork = ast_calloc(1, sizeof(*audiofork)))) {
		return -1;
	}

	/* Now that the struct has been calloced, go ahead and initialize the string fields. */
	if (ast_string_field_init(audiofork, 512)) {
		audiofork_free(audiofork);
		return -1;
	}

	/* Setup the actual spy before creating our thread */
	if (ast_audiohook_init(&audiofork->audiohook, AST_AUDIOHOOK_TYPE_SPY, audiofork_spy_type, 0)) {
		audiofork_free(audiofork);
		return -1;
	}

	/* Copy over flags and channel name */
	audiofork->flags = flags;
	if (!(audiofork->autochan = ast_autochan_setup(chan))) {
		audiofork_free(audiofork);
		return -1;
	}

	/* Direction */
	audiofork->direction = direction;

	if (direction == AST_AUDIOHOOK_DIRECTION_READ) {
		audiofork->direction_string = "in";
	}
	else if (direction == AST_AUDIOHOOK_DIRECTION_WRITE) {
		audiofork->direction_string = "out";
	}
	else {
		audiofork->direction_string = "both";
	}

	ast_verb(2, "<%s> [AudioFork] (%s) Setting Direction\n", ast_channel_name(chan), audiofork->direction_string);

	// TODO: make this configurable
	audiofork->reconnection_attempts = reconn_attempts;
	// 5 seconds
	audiofork->reconnection_timeout = reconn_timeout;

	ast_verb(2, "<%s> [AudioFork] Setting reconnection attempts to %d\n", ast_channel_name(chan), audiofork->reconnection_attempts);
	ast_verb(2, "<%s> [AudioFork] Setting reconnection timeout to %d\n", ast_channel_name(chan), audiofork->reconnection_timeout);

	/* Audio format / websocket handshake options */
	audiofork->sample_rate = sample_rate;
	audiofork->codec = ast_strdup(S_OR(codec, AUDIOFORK_DEFAULT_CODEC));
	audiofork->subprotocol = ast_strlen_zero(subprotocol) ? NULL : ast_strdup(subprotocol);
	audiofork->headers = ast_strlen_zero(headers) ? NULL : ast_strdup(headers);
	audiofork->send_metadata = send_metadata;

	audiofork->format = audiofork_get_format(audiofork->codec, &audiofork->sample_rate);
	if (!audiofork->format) {
		ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) Failed to resolve audio format '%s'\n",
			ast_channel_name(chan), audiofork->direction_string, audiofork->codec);
		audiofork_free(audiofork);
		return -1;
	}
	ast_verb(2, "<%s> [AudioFork] (%s) Audio format: codec=%s rate=%d\n",
		ast_channel_name(chan), audiofork->direction_string, audiofork->codec, audiofork->sample_rate);

	/* Server */
	if (!ast_strlen_zero(wsserver)) {
		ast_verb(2, "<%s> [AudioFork] (%s) Setting wsserver: %s\n", ast_channel_name(chan), audiofork->direction_string, wsserver);
		audiofork->wsserver = ast_strdup(wsserver);
	}

	/* TLS */
	audiofork->has_tls = 0;
	if (!ast_strlen_zero(tcert)) {
		ast_verb(2, "<%s> [AudioFork] (%s) Setting TLS Cert: %s\n", ast_channel_name(chan), audiofork->direction_string, tcert);
		struct ast_tls_config  *ast_tls_config;
		audiofork->tls_cfg = ast_calloc(1, sizeof(*ast_tls_config));
		audiofork->tls_cfg->enabled = 1;
		audiofork->has_tls = 1;
		ast_set_flag(&audiofork->tls_cfg->flags, AST_SSL_DONT_VERIFY_SERVER);
	}

	if (setup_audiofork_ds(audiofork, chan, &datastore_id, beep_id)) {
		ast_autochan_destroy(audiofork->autochan);
		audiofork_free(audiofork);
		ast_free(datastore_id);
		return -1;
	}

	ast_verb(2, "<%s> [AudioFork] (%s) Completed Setup\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
	if (!ast_strlen_zero(uid_channel_var)) {
		if (datastore_id) {
			pbx_builtin_setvar_helper(chan, uid_channel_var, datastore_id);
		}
	}

	ast_free(datastore_id);
	audiofork->name = ast_strdup(ast_channel_name(chan));

	if (!ast_strlen_zero(postprocess2)) {
		audiofork->post_process = ast_strdup(postprocess2);
	}

	ast_set_flag(&audiofork->audiohook, AST_AUDIOHOOK_TRIGGER_SYNC);
	if ((ast_test_flag(audiofork, MUXFLAG_RWSYNC))) {
		ast_set_flag(&audiofork->audiohook, AST_AUDIOHOOK_SUBSTITUTE_SILENCE);
	}

	if (readvol)
		audiofork->audiohook.options.read_volume = readvol;
	if (writevol)
		audiofork->audiohook.options.write_volume = writevol;

	if (start_audiofork(chan, &audiofork->audiohook)) {
		ast_log(LOG_WARNING, "<%s> (%s) [AudioFork] Unable to add spy type '%s'\n", audiofork->direction_string, ast_channel_name(chan), audiofork_spy_type);
		ast_audiohook_destroy(&audiofork->audiohook);
		audiofork_free(audiofork);
		return -1;
	}

	ast_verb(2, "<%s> [AudioFork] (%s) Added AudioHook Spy\n", ast_channel_name(chan), audiofork->direction_string);

	/* reference be released at audiofork destruction */
	audiofork->callid = ast_read_threadstorage_callid();

	return ast_pthread_create_detached_background(&thread, NULL, audiofork_thread, audiofork);
}

static int audiofork_exec(struct ast_channel *chan, const char *data)
{
	int x, readvol = 0, writevol = 0;
	char *uid_channel_var = NULL;
	char beep_id[64] = "";
	unsigned int direction = 2;

	struct ast_flags flags = { 0 };
	char *parse;
	char *tcert = NULL;
	int reconn_timeout = 5;
	int reconn_attempts = 5;
	int sample_rate = 8000;
	int send_metadata = 0;
	char *codec = NULL;
	char *subprotocol = NULL;
	char *headers = NULL;
	AST_DECLARE_APP_ARGS(args, 
		AST_APP_ARG(wsserver);
		AST_APP_ARG(options);
		AST_APP_ARG(post_process);
	);

	ast_log(LOG_NOTICE, "AudioFork created with args %s\n", data);
	if (ast_strlen_zero(data)) {
		ast_log(LOG_WARNING, "AudioFork requires an argument wsserver\n");
		return -1;
	}

	parse = ast_strdupa(data);

	AST_STANDARD_APP_ARGS(args, parse);

	if (args.options) {
		char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };

		ast_app_parse_options(audiofork_opts, &flags, opts, args.options);

		if (ast_test_flag(&flags, MUXFLAG_READVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_READVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the heard volume ('v') option.\n");
			} else if ((sscanf(opts[OPT_ARG_READVOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Heard volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_READVOLUME]);
			} else {
				readvol = get_volfactor(x);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_WRITEVOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_WRITEVOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the spoken volume ('V') option.\n");
			} else if ((sscanf(opts[OPT_ARG_WRITEVOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Spoken volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_WRITEVOLUME]);
			} else {
				writevol = get_volfactor(x);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_VOLUME)) {
			if (ast_strlen_zero(opts[OPT_ARG_VOLUME])) {
				ast_log(LOG_WARNING, "No volume level was provided for the combined volume ('W') option.\n");
			} else if ((sscanf(opts[OPT_ARG_VOLUME], "%2d", &x) != 1) || (x < -4) || (x > 4)) {
				ast_log(LOG_NOTICE, "Combined volume must be a number between -4 and 4, not '%s'\n", opts[OPT_ARG_VOLUME]);
			} else {
				readvol = writevol = get_volfactor(x);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_UID)) {
			uid_channel_var = opts[OPT_ARG_UID];
		}

		if (ast_test_flag(&flags, MUXFLAG_BEEP)) {
			const char *interval_str = S_OR(opts[OPT_ARG_BEEP_INTERVAL], "15");
			unsigned int interval = 15;

			if (sscanf(interval_str, "%30u", &interval) != 1) {
				ast_log(LOG_WARNING, "Invalid interval '%s' for periodic beep. Using default of %u\n", interval_str, interval);
			}

			if (ast_beep_start(chan, interval, beep_id, sizeof(beep_id))) {
				ast_log(LOG_WARNING, "Unable to enable periodic beep, please ensure func_periodic_hook is loaded.\n");
				return -1;
			}
		}
		if (ast_test_flag(&flags, MUXFLAG_DIRECTION)) {
			const char *direction_str = opts[OPT_ARG_DIRECTION];

			if (!strcmp(direction_str, "in")) {
				direction = AST_AUDIOHOOK_DIRECTION_READ;
			} else if (!strcmp(direction_str, "out")) {
				direction = AST_AUDIOHOOK_DIRECTION_WRITE;
			} else if (!strcmp(direction_str, "both")) {
				direction = AST_AUDIOHOOK_DIRECTION_BOTH;
			} else {
				direction = AST_AUDIOHOOK_DIRECTION_BOTH;

				ast_log(LOG_WARNING, "Invalid direction '%s' given. Using default of 'both'\n", opts[OPT_ARG_DIRECTION]);
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_TLS)) {
			tcert = ast_strdup ( S_OR(opts[OPT_ARG_TLS], "") );
			ast_verb(2, "Parsing TLS result tcert: %s\n", tcert);
		}

		if (ast_test_flag(&flags, MUXFLAG_RECONNECTION_TIMEOUT)) {
			reconn_timeout = atoi( S_OR(opts[OPT_ARG_RECONNECTION_TIMEOUT], "15") );
			ast_verb(2, "Reconnection timeout set to: %d\n", reconn_timeout);
		}

		if (ast_test_flag(&flags, MUXFLAG_RECONNECTION_ATTEMPTS)) {
			reconn_attempts = atoi( S_OR(opts[OPT_ARG_RECONNECTION_ATTEMPTS], "15") );
			ast_verb(2, "Reconnection attempts set to: %d\n", reconn_attempts);
		}

		if (ast_test_flag(&flags, MUXFLAG_SAMPLE_RATE)) {
			sample_rate = atoi( S_OR(opts[OPT_ARG_SAMPLE_RATE], "8000") );
			if (sample_rate <= 0) {
				ast_log(LOG_WARNING, "Invalid sample rate '%s', defaulting to 8000\n", opts[OPT_ARG_SAMPLE_RATE]);
				sample_rate = 8000;
			}
			ast_verb(2, "Sample rate set to: %d\n", sample_rate);
		}

		if (ast_test_flag(&flags, MUXFLAG_CODEC)) {
			codec = ast_strdup( S_OR(opts[OPT_ARG_CODEC], AUDIOFORK_DEFAULT_CODEC) );
			ast_verb(2, "Codec set to: %s\n", codec);
		}

		if (ast_test_flag(&flags, MUXFLAG_SUBPROTOCOL)) {
			subprotocol = ast_strdup( S_OR(opts[OPT_ARG_SUBPROTOCOL], "") );
			ast_verb(2, "WebSocket subprotocol set to: %s\n", subprotocol);
		}

		if (ast_test_flag(&flags, MUXFLAG_HEADERS)) {
			headers = ast_strdup( S_OR(opts[OPT_ARG_HEADERS], "") );
			ast_verb(2, "Custom handshake headers set to: %s\n", headers);
		}

		if (ast_test_flag(&flags, MUXFLAG_AUTH)) {
			const char *token = S_OR(opts[OPT_ARG_AUTH], "");
			if (!ast_strlen_zero(token)) {
				char *auth_header;
				if (ast_asprintf(&auth_header, "Authorization: Bearer %s", token) == -1) {
					ast_log(LOG_ERROR, "Failed to allocate memory for auth header.\n");
				} else if (ast_strlen_zero(headers)) {
					headers = auth_header;
				} else {
					char *combined;
					if (ast_asprintf(&combined, "%s,%s", headers, auth_header) == -1) {
						ast_log(LOG_ERROR, "Failed to combine auth header with custom headers.\n");
					} else {
						ast_free(headers);
						headers = combined;
					}
					ast_free(auth_header);
				}
			}
		}

		if (ast_test_flag(&flags, MUXFLAG_METADATA)) {
			send_metadata = 1;
			ast_verb(2, "Metadata transmission enabled\n");
		}

		/* Validate and sanitise the combined custom-header string (custom -H
		 * headers and/or the -A Bearer token) to reject malformed pairs and
		 * strip control characters that could be used for header injection. */
		if (!ast_strlen_zero(headers)) {
			char clean[1024];
			audiofork_normalize_headers(headers, clean, sizeof(clean));
			ast_free(headers);
			headers = ast_strlen_zero(clean) ? NULL : ast_strdup(clean);
		}
	}

	/* If there are no file writing arguments/options for the mix monitor, send a warning message and return -1 */

	if (ast_strlen_zero(args.wsserver)) {
		ast_log(LOG_WARNING, "AudioFork requires an argument (wsserver)\n");
		return -1;
	}

	pbx_builtin_setvar_helper(chan, "AUDIOFORK_WSSERVER", args.wsserver);

	/* If launch_monitor_thread works, the module reference must not be released until it is finished. */
	ast_module_ref(ast_module_info->self);

	if (launch_audiofork_thread(
		chan,
		args.wsserver,
		flags.flags,
		direction,
		tcert,
		reconn_timeout,
		reconn_attempts,
		readvol,
		writevol,
		args.post_process, 
		uid_channel_var, 
		beep_id,
		sample_rate,
		codec,
		subprotocol,
		headers,
		send_metadata)
	) {

		/* Failed */
		ast_module_unref(ast_module_info->self);
	}

	ast_free(tcert);
	ast_free(codec);
	ast_free(subprotocol);
	ast_free(headers);

	return 0;
}

static int stop_audiofork_full(struct ast_channel *chan, const char *data)
{
	struct ast_datastore *datastore = NULL;
	char *parse = "";
	struct audiofork_ds *audiofork_ds;
	const char *beep_id = NULL;

	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(audioforkid););

	if (!ast_strlen_zero(data)) {
		parse = ast_strdupa(data);
	}

	AST_STANDARD_APP_ARGS(args, parse);

	ast_channel_lock(chan);

	datastore = ast_channel_datastore_find(chan, &audiofork_ds_info, S_OR(args.audioforkid, NULL));
	if (!datastore) {
		ast_channel_unlock(chan);
		return -1;
	}
	audiofork_ds = datastore->data;

	ast_mutex_lock(&audiofork_ds->lock);

	/* The audiofork thread may be waiting on the audiohook trigger.
	 * In order to exit from the audiofork loop before waiting on channel
	 * destruction, poke the audiohook trigger. */
	if (audiofork_ds->audiohook) {
		if (audiofork_ds->audiohook->status != AST_AUDIOHOOK_STATUS_DONE) {
			ast_audiohook_update_status(audiofork_ds->audiohook, AST_AUDIOHOOK_STATUS_SHUTDOWN);
		}
		ast_audiohook_lock(audiofork_ds->audiohook);
		ast_cond_signal(&audiofork_ds->audiohook->trigger);
		ast_audiohook_unlock(audiofork_ds->audiohook);
		audiofork_ds->audiohook = NULL;
	}

	if (!ast_strlen_zero(audiofork_ds->beep_id)) {
		beep_id = ast_strdupa(audiofork_ds->beep_id);
	}

	ast_mutex_unlock(&audiofork_ds->lock);

	/* Remove the datastore so the monitor thread can exit */
	if (!ast_channel_datastore_remove(chan, datastore)) {
		ast_datastore_free(datastore);
	}

	ast_channel_unlock(chan);

	if (!ast_strlen_zero(beep_id)) {
		ast_beep_stop(chan, beep_id);
	}

	return 0;
}

static int stop_audiofork_exec(struct ast_channel *chan, const char *data)
{
	stop_audiofork_full(chan, data);
	return 0;
}

static char *handle_cli_audiofork(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	struct ast_channel *chan;
	struct ast_datastore *datastore = NULL;
	struct audiofork_ds *audiofork_ds = NULL;

	switch (cmd) {
		case CLI_INIT:
			e->command = "audiofork {start|stop|list}";
			e->usage =
				"Usage: audiofork start <chan_name> [args]\n"
				"         The optional arguments are passed to the AudioFork application.\n"
				"       audiofork stop <chan_name> [args]\n"
				"         The optional arguments are passed to the StopAudioFork application.\n"
				"       audiofork list <chan_name>\n";
			return NULL;
		case CLI_GENERATE:
			return ast_complete_channels(a->line, a->word, a->pos, a->n, 2);
	}

	if (a->argc < 3) {
		return CLI_SHOWUSAGE;
	}

	if (!(chan = ast_channel_get_by_name_prefix(a->argv[2], strlen(a->argv[2])))) {
		ast_cli(a->fd, "No channel matching '%s' found.\n", a->argv[2]);
		/* Technically this is a failure, but we don't want 2 errors printing out */
		return CLI_SUCCESS;
	}

	if (!strcasecmp(a->argv[1], "start")) {
		audiofork_exec(chan, (a->argc >= 4) ? a->argv[3] : "");
	} else if (!strcasecmp(a->argv[1], "stop")) {
		stop_audiofork_exec(chan, (a->argc >= 4) ? a->argv[3] : "");
	} else if (!strcasecmp(a->argv[1], "list")) {
		ast_cli(a->fd, "AudioFork ID\tWs Server\tReceive File\tTransmit File\n");
		ast_cli(a->fd,
						"=========================================================================\n");
		ast_channel_lock(chan);
		AST_LIST_TRAVERSE(ast_channel_datastores(chan), datastore, entry) {
			if (datastore->info == &audiofork_ds_info) {
				char *wsserver = "";
				char *filename_read = "";
				char *filename_write = "";

				audiofork_ds = datastore->data;
				if (audiofork_ds->wsserver) {
					wsserver = audiofork_ds->wsserver;
				}
				ast_cli(a->fd, "%p\t%s\t%s\t%s\n", audiofork_ds, wsserver,
								filename_read, filename_write);
			}
		}
		ast_channel_unlock(chan);
	} else {
		chan = ast_channel_unref(chan);
		return CLI_SHOWUSAGE;
	}

	chan = ast_channel_unref(chan);

	return CLI_SUCCESS;
}

/*! \brief  Mute / unmute  a MixMonitor channel */
static int manager_mute_audiofork(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *state = astman_get_header(m, "State");
	const char *direction = astman_get_header(m, "Direction");
	int clearmute = 1;
	enum ast_audiohook_flags flag;

	if (ast_strlen_zero(direction)) {
		astman_send_error(s, m, "No direction specified. Must be read, write or both");
		return AMI_SUCCESS;
	}

	if (!strcasecmp(direction, "read")) {
		flag = AST_AUDIOHOOK_MUTE_READ;
	} else if (!strcasecmp(direction, "write")) {
		flag = AST_AUDIOHOOK_MUTE_WRITE;
	} else if (!strcasecmp(direction, "both")) {
		flag = AST_AUDIOHOOK_MUTE_READ | AST_AUDIOHOOK_MUTE_WRITE;
	} else {
		astman_send_error(s, m, "Invalid direction specified. Must be read, write or both");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	if (ast_strlen_zero(state)) {
		astman_send_error(s, m, "No state specified");
		return AMI_SUCCESS;
	}

	clearmute = ast_false(state);

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (ast_audiohook_set_mute(c, audiofork_spy_type, flag, clearmute)) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Cannot set mute flag");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	astman_append(s, "\r\n");

	ast_channel_unref(c);

	return AMI_SUCCESS;
}

static int manager_audiofork(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *wsserver = astman_get_header(m, "WsServer");
	const char *options = astman_get_header(m, "Options");
	//const char *command = astman_get_header(m, "Command");
	char *opts[OPT_ARG_ARRAY_SIZE] = { NULL, };
	struct ast_flags flags = { 0 };
	char *uid_channel_var = NULL;
	const char *audiofork_id = NULL;
	int res;
	char args[PATH_MAX];

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	if (!ast_strlen_zero(options)) {
		ast_app_parse_options(audiofork_opts, &flags, opts, ast_strdupa(options));
	}

	snprintf(args, sizeof(args), "%s,%s", wsserver, options);

	res = audiofork_exec(c, args);

	if (ast_test_flag(&flags, MUXFLAG_UID)) {
		uid_channel_var = opts[OPT_ARG_UID];
		ast_channel_lock(c);
		audiofork_id = pbx_builtin_getvar_helper(c, uid_channel_var);
		audiofork_id = ast_strdupa(S_OR(audiofork_id, ""));
		ast_channel_unlock(c);
	}

	if (res) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Could not start monitoring channel");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	if (!ast_strlen_zero(audiofork_id)) {
		astman_append(s, "AudioForkID: %s\r\n", audiofork_id);
	}

	astman_append(s, "\r\n");

	ast_channel_unref(c);

	return AMI_SUCCESS;
}

static int manager_stop_audiofork(struct mansession *s, const struct message *m)
{
	struct ast_channel *c;
	const char *name = astman_get_header(m, "Channel");
	const char *id = astman_get_header(m, "ActionID");
	const char *audiofork_id = astman_get_header(m, "AudioForkID");
	int res;

	if (ast_strlen_zero(name)) {
		astman_send_error(s, m, "No channel specified");
		return AMI_SUCCESS;
	}

	c = ast_channel_get_by_name(name);
	if (!c) {
		astman_send_error(s, m, "No such channel");
		return AMI_SUCCESS;
	}

	res = stop_audiofork_full(c, audiofork_id);
	if (res) {
		ast_channel_unref(c);
		astman_send_error(s, m, "Could not stop monitoring channel");
		return AMI_SUCCESS;
	}

	astman_append(s, "Response: Success\r\n");

	if (!ast_strlen_zero(id)) {
		astman_append(s, "ActionID: %s\r\n", id);
	}

	astman_append(s, "\r\n");

	ast_channel_unref(c);

	return AMI_SUCCESS;
}

static int func_audiofork_read(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct ast_datastore *datastore;
	struct audiofork_ds *ds_data;
	AST_DECLARE_APP_ARGS(args, AST_APP_ARG(id); AST_APP_ARG(key););

	AST_STANDARD_APP_ARGS(args, data);

	if (ast_strlen_zero(args.id) || ast_strlen_zero(args.key)) {
		ast_log(LOG_WARNING, "Not enough arguments provided to %s. An ID and key must be provided\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &audiofork_ds_info, args.id);
	ast_channel_unlock(chan);

	if (!datastore) {
		ast_log(LOG_WARNING, "Could not find AudioFork with ID %s\n", args.id);
		return -1;
	}

	ds_data = datastore->data;

	if (!strcasecmp(args.key, "filename")) {
		ast_copy_string(buf, ds_data->wsserver, len);
	} else {
		ast_log(LOG_WARNING, "Unrecognized %s option %s\n", cmd, args.key);
		return -1;
	}
	return 0;
}

static struct ast_custom_function audiofork_function = {
	.name = "AUDIOFORK",
	.read = func_audiofork_read,
};

static struct ast_cli_entry cli_audiofork[] = {
	AST_CLI_DEFINE(handle_cli_audiofork, "Execute a AudioFork command")
};

static int set_audiofork_methods(void)
{
	return 0;
}

static int clear_audiofork_methods(void)
{
	return 0;
}

static int unload_module(void)
{
	int res;

	ast_cli_unregister_multiple(cli_audiofork, ARRAY_LEN(cli_audiofork));
	res = ast_unregister_application(stop_app);
	res |= ast_unregister_application(app);
	res |= ast_manager_unregister("AudioForkMute");
	res |= ast_manager_unregister("AudioFork");
	res |= ast_manager_unregister("StopAudioFork");
	res |= ast_custom_function_unregister(&audiofork_function);
	res |= clear_audiofork_methods();

	return res;
}

static int load_module(void)
{
	int res;

	ast_cli_register_multiple(cli_audiofork, ARRAY_LEN(cli_audiofork));
	res = ast_register_application_xml(app, audiofork_exec);
	res |= ast_register_application_xml(stop_app, stop_audiofork_exec);
	res |= ast_manager_register_xml("AudioForkMute", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, manager_mute_audiofork);
	res |= ast_manager_register_xml("AudioFork", EVENT_FLAG_SYSTEM, manager_audiofork);
	res |= ast_manager_register_xml("StopAudioFork", EVENT_FLAG_SYSTEM | EVENT_FLAG_CALL, manager_stop_audiofork);
	res |= ast_custom_function_register(&audiofork_function);
	res |= set_audiofork_methods();

	return res;
}

AST_MODULE_INFO(
	ASTERISK_GPL_KEY, 
	AST_MODFLAG_DEFAULT,
	"Audio Forking application",
	.support_level = AST_MODULE_SUPPORT_CORE,
	.load = load_module,
	.unload = unload_module,
	.optional_modules = "func_periodic_hook",
);
