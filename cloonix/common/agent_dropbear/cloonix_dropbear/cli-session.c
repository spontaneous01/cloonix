/*
 * Dropbear SSH
 * 
 * Copyright (c) 2002,2003 Matt Johnston
 * Copyright (c) 2004 by Mihnea Stoenescu
 * All rights reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE. */

#include "includes.h"
#include "session.h"
#include "dbutil.h"
#include "ssh.h"
#include "packet.h"
#include "channel.h"
#include "service.h"
#include "runopts.h"
#include "chansession.h"
#include "io_clownix.h"

static void cli_remoteclosed();
void cli_sessionloop();
static void cli_session_init();
static void recv_msg_service_accept(void);
static void recv_msg_global_request_cli(void);

struct clientsession cli_ses; /* GLOBAL */

/* Sorted in decreasing frequency will be more efficient - data and window
 * should be first */
static const packettype cli_packettypes[] = {
	/* TYPE, FUNCTION */
	{SSH_MSG_CHANNEL_DATA, recv_msg_channel_data},
	{SSH_MSG_CHANNEL_EXTENDED_DATA, recv_msg_channel_extended_data},
	{SSH_MSG_CHANNEL_WINDOW_ADJUST, recv_msg_channel_window_adjust},
	{SSH_MSG_SERVICE_ACCEPT, recv_msg_service_accept}, /* client */
	{SSH_MSG_CHANNEL_REQUEST, recv_msg_channel_request},
	{SSH_MSG_CHANNEL_OPEN, recv_msg_channel_open},
	{SSH_MSG_CHANNEL_EOF, recv_msg_channel_eof},
	{SSH_MSG_CHANNEL_CLOSE, recv_msg_channel_close},
	{SSH_MSG_CHANNEL_OPEN_CONFIRMATION, recv_msg_channel_open_confirmation},
	{SSH_MSG_CHANNEL_OPEN_FAILURE, recv_msg_channel_open_failure},
	{SSH_MSG_GLOBAL_REQUEST, recv_msg_global_request_cli},
	{SSH_MSG_CHANNEL_SUCCESS, ignore_recv_response},
	{SSH_MSG_CHANNEL_FAILURE, ignore_recv_response},
	{SSH_MSG_REQUEST_SUCCESS, ignore_recv_response},
	{SSH_MSG_REQUEST_FAILURE, ignore_recv_response},
	{0, 0} /* End */
};



void cli_session(int sock_in, int sock_out)
{
  common_session_init(sock_in, sock_out);
  ses.chantype = &clichansess;
  ses.channel.ctype = &clichansess;
  cli_session_init();
  sessinitdone = 1;
  send_session_identification();
  ses.authstate.authdone = 1;
  cli_ses.state = USERAUTH_SUCCESS_RCVD;
  cli_sessionloop();
}


static void cli_session_init() {

	cli_ses.state = STATE_NOTHING;

	cli_ses.tty_raw_mode = 0;
	cli_ses.winchange = 0;

	cli_ses.stdincopy = dup(STDIN_FILENO);
	cli_ses.stdinflags = fcntl(STDIN_FILENO, F_GETFL, 0);
	cli_ses.stdoutcopy = dup(STDOUT_FILENO);
	cli_ses.stdoutflags = fcntl(STDOUT_FILENO, F_GETFL, 0);
	cli_ses.stderrcopy = dup(STDERR_FILENO);
	cli_ses.stderrflags = fcntl(STDERR_FILENO, F_GETFL, 0);

	cli_ses.retval = EXIT_SUCCESS; 


	/* For printing "remote host closed" for the user */
	ses.remoteclosed = cli_remoteclosed;


	/* packet handlers */
	ses.packettypes = cli_packettypes;

	ses.isserver = 0;


}

static void send_msg_service_request(char* servicename) {
	buf_putbyte(ses.writepayload, SSH_MSG_SERVICE_REQUEST);
	buf_putstring(ses.writepayload, servicename, strlen(servicename));
	encrypt_packet();
}

static void recv_msg_service_accept(void) {
	/* do nothing, if it failed then the server MUST have disconnected */
}

void cli_sessionloop_winchange() {

  if ((cli_ses.state == SESSION_RUNNING) &&
      (cli_ses.winchange)) 
   cli_chansess_winchange();
}



/* This function drives the progress of the session - it initiates KEX,
 * service, userauth and channel requests */
void cli_sessionloop() {


	switch (cli_ses.state) {

		case STATE_NOTHING:
			send_msg_service_request(SSH_SERVICE_USERAUTH);
			cli_ses.state = USERAUTH_REQ_SENT;
			return;

		case USERAUTH_REQ_SENT:
			return;
			

		case USERAUTH_SUCCESS_RCVD:
			cli_send_chansess_request();
			cli_ses.state = SESSION_RUNNING;
			return;

		case SESSION_RUNNING:
                        if (ses.channel.init_done == 0)
                          session_cleanup();

			if (cli_ses.winchange) {
				cli_chansess_winchange();
			}
			return;


	default:
		break;
	}


}




static void cli_remoteclosed() {

	m_close(ses.sock_in);
	m_close(ses.sock_out);
	ses.sock_in = -1;
	ses.sock_out = -1;
	KERR("Remote closed the connection");
}

/* Operates in-place turning dirty (untrusted potentially containing control
 * characters) text into clean text. 
 * Note: this is safe only with ascii - other charsets could have problems. */
void cleantext(unsigned char* dirtytext) {

	unsigned int i, j;
	unsigned char c;

	j = 0;
	for (i = 0; dirtytext[i] != '\0'; i++) {

		c = dirtytext[i];
		/* We can ignore '\r's */
		if ( (c >= ' ' && c <= '~') || c == '\n' || c == '\t') {
			dirtytext[j] = c;
			j++;
		}
	}
	/* Null terminate */
	dirtytext[j] = '\0';
}

static void recv_msg_global_request_cli(void) {
	/* Send a proper rejection */
	send_msg_request_failure();
}
