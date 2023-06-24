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
	char *direction_string;
	int reconnection_attempts;
	int reconnection_timeout;
	char *post_process;
	char *name;
	ast_callid callid;
	unsigned int flags;
	struct ast_autochan *autochan;
	struct audiofork_ds *audiofork_ds;

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
	MUXFLAG_RECONNECTION_ATTEMPTS = (1 << 17),
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
};

static void audiofork_ds_destroy(void *data)
{
	struct audiofork_ds *audiofork_ds = data;

	ast_mutex_lock(&audiofork_ds->lock);
	audiofork_ds->audiohook = NULL;
	audiofork_ds->destruction_ok = 1;
	ast_free(audiofork_ds->wsserver);
	ast_free(audiofork_ds->beep_id);
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
	ast_verb(2, "[AudioFork] Closing websocket connecion\n");
	if (audiofork->websocket) {
		ast_verb(2, "[AudioFork] Calling ast_websocket_close\n");
		return ast_websocket_close(audiofork->websocket, 1011);
	}

	ast_verb(2, "[AudioFork] No reference to websocket, can't close connection\n");
	return -1;
}


/*
	1 = success
	0 = fail
*/
static enum ast_websocket_result audiofork_ws_connect(struct audiofork *audiofork)
{
	enum ast_websocket_result result;

	if (audiofork->websocket) {
		ast_verb(2, "<%s> [AudioFork] (%s) Reconnecting websocket server at: %s\n",
			ast_channel_name(audiofork->autochan->chan),
			audiofork->direction_string,
			audiofork->audiofork_ds->wsserver);

		// close the websocket connection before reconnecting
		audiofork_ws_close(audiofork);

		ao2_cleanup(audiofork->websocket);
	}
	else {
		ast_verb(2, "<%s> [AudioFork] (%s) Connecting websocket server at: %s\n",
			ast_channel_name(audiofork->autochan->chan),
			audiofork->direction_string,
			audiofork->audiofork_ds->wsserver);
	}

	// Check if we're running with TLS
	if (audiofork->has_tls == 1) {
		ast_verb(2, "<%s> [AudioFork] (%s) Creating WS with TLS\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
		audiofork->websocket = ast_websocket_client_create(audiofork->audiofork_ds->wsserver, "echo", audiofork->tls_cfg, &result);
	} else {
		ast_verb(2, "<%s> [AudioFork] (%s) Creating WS without TLS\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
		audiofork->websocket = ast_websocket_client_create(audiofork->audiofork_ds->wsserver, "echo", NULL, &result);
	}

	return result;
}

/*
	reconn_status
	0 = oK
	1 = FAILED
*/
static int audiofork_start_reconnecting(struct audiofork *audiofork)
{
	int counter= 0;
	int status = 0;
	int timeout = audiofork->reconnection_timeout;
	int attempts = audiofork->reconnection_attempts;
	int last_attempt = 0;
	int now;
	int delta;
	int result;

	while (counter < attempts) {
		now = (int)time(NULL);
		delta = now - last_attempt;
		//ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) Reconnection delta %d\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, reconn_delta);

		// small check to see if we should keep waiting on reconnection attempts...
		// basically this checks if reconnection wasnt already initiated, or if it was, it ensures that the reconnection wait is still less than the max allowed timeout
		if (last_attempt != 0 && delta <= timeout) {
			// keep waiting
			continue;
		}

		// try to reconnect
		result = audiofork_ws_connect(audiofork);
		if (result == WS_OK) {
			status = 0;
			last_attempt = 0;
			break;
		}

		// reconnection failed...
		// update our counter for last reconnection attempt
		last_attempt=(int)time(NULL);

		ast_log(LOG_ERROR, "<%s> [AudioFork] (%s) Reconnection failed... trying again in %d seconds. %d attempts remaining reconn_now %d reconn_last_attempt %d\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, timeout, (attempts-counter), now, last_attempt);

		counter ++;
		status = 1;
	}

	return status;
}

static void audiofork_free(struct audiofork *audiofork)
{
	if (audiofork) {
		if (audiofork->audiofork_ds) {
			ast_mutex_destroy(&audiofork->audiofork_ds->lock);
			ast_cond_destroy(&audiofork->audiofork_ds->destruction_condition);
			ast_free(audiofork->audiofork_ds);
		}

		ast_free(audiofork->name);
		ast_free(audiofork->post_process);
		ast_free(audiofork->wsserver);
		ast_free(audiofork->direction_string);

		audiofork_ws_close(audiofork);
		ao2_cleanup(audiofork->websocket);

		/* clean stringfields */
		ast_string_field_free_memory(audiofork);

		ast_free(audiofork);
	}
}



static void *audiofork_thread(void *obj)
{
	struct audiofork *audiofork = obj;
	struct ast_format *format_slin;
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

		ast_test_suite_event_notify("AUDIOFORK_END", "File: %s\r\n", audiofork->wsserver);

		/* kill the audiohook */
		destroy_monitor_audiohook(audiofork);
		ast_autochan_destroy(audiofork->autochan);

		/* We specifically don't do audiofork_free(audiofork) here because the automatic datastore cleanup will get it */

		ast_module_unref(ast_module_info->self);

		return 0;
	}

	ast_verb(2, "<%s> [AudioFork] (%s) Begin AudioFork Recording %s\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string, audiofork->name);

	//fs = &audiofork->audiofork_ds->fs;

	ast_mutex_lock(&audiofork->audiofork_ds->lock);
	format_slin = ast_format_cache_get_slin_by_rate(audiofork->audiofork_ds->samp_rate);

	ast_mutex_unlock(&audiofork->audiofork_ds->lock);

	/* The audiohook must enter and exit the loop locked */
	ast_audiohook_lock(&audiofork->audiohook);

	while (audiofork->audiohook.status == AST_AUDIOHOOK_STATUS_RUNNING) {
		// ast_verb(2, "<%s> [AudioFork] (%s) Reading Audio Hook frame...\n", ast_channel_name(audiofork->autochan->chan), audiofork->direction_string);
		struct ast_frame *fr = ast_audiohook_read_frame(&audiofork->audiohook, SAMPLES_PER_FRAME, audiofork->direction, format_slin);

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

	/* Datastore cleanup.  close the filestream and wait for ds destruction */
	ast_mutex_lock(&audiofork->audiofork_ds->lock);
	if (!audiofork->audiofork_ds->destruction_ok) {
		ast_cond_wait(&audiofork->audiofork_ds->destruction_condition, &audiofork->audiofork_ds->lock);
	}
	ast_mutex_unlock(&audiofork->audiofork_ds->lock);

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
	ast_test_suite_event_notify("AUDIOFORK_END", "File: %s\r\n", audiofork->wsserver);

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

	audiofork_ds->samp_rate = 8000;
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
	const char *beep_id
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
	AST_DECLARE_APP_ARGS(args, 
		AST_APP_ARG(wsserver);
		AST_APP_ARG(options);
		AST_APP_ARG(post_process);
	);

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
		beep_id)
	) {

		/* Failed */
		ast_module_unref(ast_module_info->self);
	}

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
		ast_cli(a->fd, "AudioFork ID\tFile\tReceive File\tTransmit File\n");
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
	const char *file = astman_get_header(m, "File");
	const char *options = astman_get_header(m, "Options");
	const char *command = astman_get_header(m, "Command");
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

	snprintf(args, sizeof(args), "%s,%s,%s", file, options, command);

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
