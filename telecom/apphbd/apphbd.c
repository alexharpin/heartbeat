/*
 * apphbd:	application heartbeat daemon
 *
 * This daemon implements an application heartbeat server.
 *
 * Clients register with it and are expected to check in from time to time
 * If they don't, we complain ;-)
 *
 * More details can be found in the <apphb.h> header file.
 *
 * Copyright(c) 2002 Alan Robertson <alanr@unix.sh>
 *
 *********************************************************************
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * TODO list:
 *
 *	- Make it a real production-grade daemon process...
 * 
 *	- change all the fprintfs
 *
 *	- Log things in the event log
 *
 *	- Implement plugins for (other) notification mechanisms...
 * 
 */

#include <portability.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <apphb.h>
#define	time	footime
#define	index	fooindex
#include	<glib.h>
#undef time
#undef index
#include <clplumbing/longclock.h>
#include <clplumbing/ipc.h>
#include <clplumbing/Gmain_timeout.h>
#include <clplumbing/apphb_cs.h>

typedef struct apphb_client apphb_client_t;

/*
 * Per-client data structure.
 */
struct apphb_client {
	char *			appname;	/* application name */
	pid_t			pid;		/* application pid */
	guint			timerid;	/* timer source id */
	guint			sourceid;	/* message source id */
	long			timerms;	/* heartbeat timeout in ms */
	gboolean		missinghb;	/* True if missing a hb */
	struct OCF_IPC_CHANNEL*	ch;		/* client comm channel */
	GPollFD*		ifd;		/* ifd for poll */
	GPollFD*		ofd;		/* ofd for poll */
	struct OCF_IPC_MESSAGE	rcmsg;		/* return code msg */
	struct apphb_rc		rc;		/* last return code */
	gboolean		deleteme;	/* Delete after next call */
};

void apphb_client_remove(apphb_client_t* client);
static void apphb_putrc(apphb_client_t* client, int rc);
static gboolean	apphb_timer_popped(gpointer data);
static apphb_client_t* apphb_client_new(struct OCF_IPC_CHANNEL* ch);
static int apphb_client_register(apphb_client_t* client, void* Msg, int len);
static void apphb_read_msg(apphb_client_t* client);
static int apphb_client_hb(apphb_client_t* client, void * msg, int msgsize);
void apphb_process_msg(apphb_client_t* client, void* msg,  int length);

/* gmainloop event "source" functions for client communication */
static gboolean apphb_prepare(gpointer src, GTimeVal*now, gint*timeout
,	gpointer user);
static gboolean apphb_check(gpointer src, GTimeVal*now, gpointer user);
static gboolean apphb_dispatch(gpointer src, GTimeVal*now, gpointer user);

static GSourceFuncs apphb_eventsource = {
	apphb_prepare,
	apphb_check,
	apphb_dispatch,
	NULL
};

/* gmainloop event "source" functions for new client connections */
static gboolean apphb_new_prepare(gpointer src, GTimeVal*now, gint*timeout
,	gpointer user);
static gboolean apphb_new_check(gpointer src, GTimeVal*now, gpointer user);
static gboolean apphb_new_dispatch(gpointer src, GTimeVal*now, gpointer user);

static GSourceFuncs apphb_connsource = {
	apphb_new_prepare,
	apphb_new_check,
	apphb_new_dispatch,
	NULL
};

/* Send return code from current operation back to client... */
static void
apphb_putrc(apphb_client_t* client, int rc)
{
	client->rc.rc = rc;

	if (client->ch->ops->send(client->ch, &client->rcmsg) != CH_SUCCESS) {
		client->deleteme = TRUE;
	}
	
}

/* Oops!  Client heartbeat timer expired! -- Bad client! */
static gboolean
apphb_timer_popped(gpointer data)
{
	apphb_client_t*	client = data;
	fprintf(stderr, "OOPS! client '%s' (pid %d) didn't heartbeat\n"
	,	client->appname, client->pid);
	client->missinghb = TRUE;
	client->timerid = 0;
	return FALSE;
}

/* gmainloop "source" prepare function */
static gboolean
apphb_prepare(gpointer Src, GTimeVal*now, gint*timeout, gpointer Client)
{
	apphb_client_t*		client  = Client;
	if (client->deleteme) {
		apphb_client_remove(client);
	}
	return FALSE;
}

/* gmainloop "source" check function */
static gboolean
apphb_check(gpointer Src, GTimeVal*now, gpointer Client)
{
	GPollFD*		src = Src;
	apphb_client_t*		client  = Client;


	client->ch->ops->resume_io(client->ch);

	return src->revents != 0
	||	client->ch->ops->is_message_pending(client->ch);
}

/* gmainloop "source" dispatch function */
static gboolean
apphb_dispatch(gpointer Src, GTimeVal* now, gpointer Client)
{
	GPollFD*		src = Src;
	apphb_client_t*		client  = Client;

	if (src->revents & G_IO_HUP) {
		fprintf(stderr, "pid: %d: client HUP!\n", getpid());
		apphb_client_remove(client);
		return FALSE;
	}

	client->ch->ops->resume_io(client->ch);

	while (client->ch->ops->is_message_pending(client->ch)) {
		apphb_read_msg(client);
	}
	return TRUE;
}
#define	DEFAULT_TO	(10*60*1000)

/* Create new client - we don't know appname or pid yet */
static apphb_client_t*
apphb_client_new(struct OCF_IPC_CHANNEL* ch)
{
	apphb_client_t*	ret;

	int	rdfd;
	int	wrfd;
	int	wrflags = G_IO_OUT|G_IO_NVAL;
	int	rdflags = G_IO_IN |G_IO_NVAL | G_IO_PRI | G_IO_HUP;

	ret = g_new(apphb_client_t, 1);

	fprintf(stderr, "Creating new apphb client\n");
	ret->appname = NULL;
	ret->ch = ch;
	ret->timerid = 0;
	ret->pid = 0;
	ret->deleteme = FALSE;
	ret->missinghb = FALSE;

	/* Create the standard result code (errno) message to send client
	 * NOTE: this disallows multiple outstanding calls from a client
	 * (IMHO this is not a problem)
	 */
	ret->rcmsg.msg_body = &ret->rc;
	ret->rcmsg.msg_len = sizeof(ret->rc);
	ret->rcmsg.msg_done = NULL;
	ret->rcmsg.msg_private = NULL;
	ret->rc.rc = 0;

	/* Prepare GPollFDs to give g_main_add_poll() */

	wrfd = ch->ops->get_send_select_fd(ch);
	rdfd = ch->ops->get_recv_select_fd(ch);

	if (rdfd == wrfd) {
		/* We only need to poll one FD */
		/* FIXME: We ought to handle output blocking */
#if 0
		rdflags |= wrflags;
#endif
		wrflags = 0;
		ret->ofd = NULL;
	}else{
		/* We have to poll both FDs separately */
		ret->ofd = g_new(GPollFD, 1);
		ret->ofd->fd = wrfd;
		ret->ofd->events = wrflags;
		g_main_add_poll(ret->ofd, G_PRIORITY_DEFAULT);
	}
	ret->ifd = g_new(GPollFD, 1);
	ret->ifd->fd = rdfd;
	ret->ifd->events = rdflags;
	g_main_add_poll(ret->ifd, G_PRIORITY_DEFAULT);

	/* Set timer for this client... */
	ret->timerid = Gmain_timeout_add(DEFAULT_TO, apphb_timer_popped, ret);

	/* Set up "real" input message source for this client */
	ret->sourceid = g_source_add(G_PRIORITY_HIGH, FALSE
	,	&apphb_eventsource, ret->ifd, ret, NULL);
	return ret;
}

/* Process client registration message */
static int
apphb_client_register(apphb_client_t* client, void* Msg,  int length)
{
	struct apphb_signupmsg*	msg = Msg;
	int			namelen = -1;

	if (client->appname) {
		return EEXIST;
	}

	if (length < sizeof(*msg)
	||	(namelen = strnlen(msg->appname, sizeof(msg->appname))) < 1
	||	namelen >= sizeof(msg->appname)) {
		return EINVAL;
	}

	if (msg->pid < 2 || (kill(msg->pid, 0) < 0 && errno != EPERM)) {
		return EINVAL;
	}

	client->pid = msg->pid;
	client->appname = g_strdup(msg->appname);
	return 0;
}


/* Shut down the requested client */
void
apphb_client_remove(apphb_client_t* client)
{
	if (client->sourceid) {
		g_source_remove(client->sourceid);
		client->sourceid=0;
	}
	if (client->timerid) {
		g_source_remove(client->timerid);
		client->timerid=0;
	}
	if (client->ifd) {
		g_main_remove_poll(client->ifd);
		g_free(client->ifd);
		client->ifd=NULL;
	}
	if (client->ofd) {
		g_main_remove_poll(client->ofd);
		g_free(client->ofd);
		client->ofd=NULL;
	}
	if (client->ch) {
		client->ch->ops->destroy(client->ch);
		client->ch = NULL;
	}
	g_free(client->appname);
	memset(client, 0, sizeof(*client));
}

/* Process disconnect message from client */
static int
apphb_client_disconnect(apphb_client_t* client , void * msg, int msgsize)
{
	client->deleteme=TRUE;
	return 0;
}

/* Establish new timeout interval for this client */
static int
apphb_client_set_timeout(apphb_client_t* client, void * Msg, int msgsize)
{
	struct apphb_msmsg*	msg = Msg;

	if (msgsize < sizeof(*msg) || msg->ms < 0) {
		return EINVAL;
	}
	client->timerms = msg->ms;
	return apphb_client_hb(client, Msg, msgsize);
}

/* Client heartbeat received */
static int
apphb_client_hb(apphb_client_t* client, void * Msg, int msgsize)
{
	if (client->missinghb) {
		fprintf(stderr, "Client '%s' (pid %d) alive again.\n"
		,	client->appname, client->pid);
		client->missinghb = FALSE;
	}
		
	if (client->timerid) {
		g_source_remove(client->timerid);
		client->timerid = 0;
	}
	if (client->timerms > 0) {
		client->timerid = Gmain_timeout_add(client->timerms
		,	apphb_timer_popped, client);
	}
	return 0;
}


/* Read and process a client request message */
static void
apphb_read_msg(apphb_client_t* client)
{
	struct OCF_IPC_MESSAGE*	msg = NULL;
	
	switch (client->ch->ops->recv(client->ch, &msg)) {

		case CH_SUCCESS:
		apphb_process_msg(client, msg->msg_body, msg->msg_len);
		if (msg->msg_done) {
			msg->msg_done(msg);
		}
		break;


		case CH_BROKEN:
		client->deleteme = TRUE;
		break;


		case CH_FAIL:
		fprintf(stderr, "OOPS! client %s (pid %d) read failure!"
		,	client->appname, client->pid);
		break;
	}
}

/*
 * Mappings between commands and strings
 */
struct hbcmd {
	const char *	msg;
	gboolean	senderrno;
	int		(*fun)(apphb_client_t* client, void* msg, int len);
};

/*
 * Put HEARTBEAT message first - it is by far the most common message...
 */
struct hbcmd	hbcmds[] =
{
	{HEARTBEAT,	FALSE, apphb_client_hb},
	{REGISTER,	TRUE, apphb_client_register},
	{SETINTERVAL,	TRUE, apphb_client_set_timeout},
	{UNREGISTER,	TRUE, apphb_client_disconnect},
};

/* Process a message from an app heartbeat client process */
void
apphb_process_msg(apphb_client_t* client, void* Msg,  int length)
{
	struct apphb_msg *	msg = Msg;
	const int		sz1	= sizeof(msg->msgtype)-1;
	int			rc	= EINVAL;
	gboolean		sendrc	= TRUE;
	int			j;


	if (length < sizeof(*msg)) {
		return;
	}

	msg->msgtype[sz1] = EOS;

	/* Which command are we processing? */

	for (j=0; j < DIMOF(hbcmds); ++j) {
		if (strcmp(msg->msgtype, hbcmds[j].msg) == 0) {
			sendrc = hbcmds[j].senderrno;

			if (client->appname == NULL
			&&	hbcmds[j].fun != apphb_client_register) {
				rc = ESRCH;
				break;
			}

			rc = hbcmds[j].fun(client, Msg, length);
		}
	}
	if (sendrc) {
		apphb_putrc(client, rc);
	}
}

/* gmainloop client connection source "prepare" function */
static gboolean
apphb_new_prepare(gpointer src, GTimeVal*now, gint*timeout
,	gpointer user)
{
	return FALSE;
}

/* gmainloop client connection source "check" function */
static gboolean
apphb_new_check(gpointer Src, GTimeVal*now, gpointer user)
{
	GPollFD*	src = Src;
	return src->revents != 0;
}

/* gmainloop client connection source "dispatch" function */
/* This is where we accept connections from a new client */
static gboolean
apphb_new_dispatch(gpointer Src, GTimeVal*now, gpointer user)
{
	struct OCF_IPC_WAIT_CONNECTION*		conn = user;
	struct OCF_IPC_CHANNEL*			newchan;

	newchan = conn->ops->accept_connection(conn, NULL);
	if (newchan != NULL) {
		/* This sets up comm channel w/client
		 * Ignoring the result value is OK, because
		 * the client registers itself w/event system.
		 */
		(void)apphb_client_new(newchan);
	}else{
		perror("accept_connection failed!");
	}
	return TRUE;
}


/*
 *	Main program for monitoring application heartbeats...
 */
int
main(int argc, char ** argv)
{
	char		path[] = PATH_ATTR;
	char		commpath[] = APPHBSOCKPATH;

	struct OCF_IPC_WAIT_CONNECTION*	wconn;
	GHashTable*	wconnattrs;

	int		wcfd;
	GPollFD		pollfd;
	GMainLoop*	mainloop;
	
	/* Create a "waiting for connection" object */

	wconnattrs = g_hash_table_new(g_str_hash, g_str_equal);

	g_hash_table_insert(wconnattrs, path, commpath);

	wconn = ipc_wait_conn_constructor(IPC_ANYTYPE, wconnattrs);

	if (wconn == NULL) {
		perror("UhOh! No wconn!");
		exit(1);
	}

	/* Set up GPollFD to watch for new connection events... */
	wcfd = wconn->ops->get_select_fd(wconn);
	pollfd.fd = wcfd;
	pollfd.events = G_IO_IN | G_IO_NVAL | G_IO_PRI | G_IO_HUP;
	pollfd.revents = 0;
	g_main_add_poll(&pollfd, G_PRIORITY_DEFAULT);

	/* Create a source to handle new connection requests */
	g_source_add(G_PRIORITY_HIGH, FALSE
	,	&apphb_connsource, &pollfd, wconn, NULL);


	/* Create the mainloop and run it... */
	mainloop = g_main_new(FALSE);
	g_main_run(mainloop);
	return 0;
}
