/* 
 * ccm.c: Consensus Cluster Service Program 
 *
 * Copyright (c) International Business Machines  Corp., 2002
 * Author: Ram Pai (linuxram@us.ibm.com)
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
 *
 */
#include <ccm.h>


extern int global_verbose;
extern int global_debug;

//
// the various states of the CCM state machine.
//
enum ccm_state  {
	CCM_STATE_NONE=0,	// is in NULL state 
	CCM_STATE_VERSION_REQUEST=10,	// sent a request for protocol version
	CCM_STATE_JOINING=20,  // has initiated a join protocol 
	CCM_STATE_RCVD_UPDATE=30,// has recevied the updates from other nodes
	CCM_STATE_SENT_MEMLISTREQ=40,// CL has sent a request for member list 
				// this state is applicable only on CL
	CCM_STATE_REQ_MEMLIST=50,// CL has requested member list
				  // this state is applicable only on non-CL
	CCM_STATE_MEMLIST_RES=60,// Responded member list to the Cluster 
				  //	Leader
	CCM_STATE_JOINED=70,    // PART of the CCM cluster membership!
	CCM_STATE_END
};

/* add new enums to this structure as and when new protocols are added */
enum ccm_protocol {
	CCM_VER_NONE = 0,
	CCM_VER_1,
	CCM_VER_LAST
};

typedef struct ccm_proto_s {
	enum ccm_protocol  com_hiproto;// highest protocol version that 
				// this node can handle
	int	com_active_proto;// protocol version
} ccm_proto_t;



#define COOKIESIZE 15
typedef struct ccm_info_s {
	llm_info_t 	ccm_llm;	//  low level membership info

	int		ccm_nodeCount;	//  number of nodes in the ccm cluster
	int		ccm_member[MAXNODE];// members of the ccm cluster
	graph_t		*ccm_graph; 	// memlist calculation graph 

	ccm_proto_t  	ccm_proto;	// protocol version information
#define ccm_active_proto ccm_proto.com_active_proto
#define ccm_hiproto	  ccm_proto.com_hiproto

	char		ccm_cookie[COOKIESIZE];// context identification string.
	uint32_t	ccm_transition_major;// transition number of the cluster
	int		ccm_cluster_leader; // cluster leader of the last major
				// transition. index of cl in ccm_member table
	int		ccm_joined_transition;
					// this indicates the major transition 
					// number during which this node became
					// a member of the cluster.
					// A sideeffect of this is it also
					// is used to figure out if this node
					// was ever a part of the cluster.
					// Should be intially set to 0
	enum ccm_state 	ccm_node_state;	// cluster state of this node 
	uint32_t	ccm_transition_minor;// minor transition number of the 
					//cluster

	ccm_update_t   ccm_update; 	// structure that keeps track
					// of uptime of each member
	GSList		*ccm_joiner_head;// keeps track of new-bees version
					// request. 
	ccm_version_t  ccm_version;     // keeps track of version request 
					// related info
} ccm_info_t;




#define		CCM_SET_ACTIVEPROTO(info, val) \
					info->ccm_active_proto = val
#define		CCM_SET_MAJORTRANS(info, val) 	\
					info->ccm_transition_major = val
#define		CCM_SET_MINORTRANS(info, val) 	\
					info->ccm_transition_minor = val
#define		CCM_INCREMENT_MAJORTRANS(info) 	\
					info->ccm_transition_major++
#define		CCM_INCREMENT_MINORTRANS(info) 	\
					info->ccm_transition_minor++
#define		CCM_RESET_MAJORTRANS(info) 	\
					info->ccm_transition_major = 0
#define		CCM_RESET_MINORTRANS(info) 	\
					info->ccm_transition_minor = 0
#define		CCM_SET_STATE(info, state) 	\
		{  \
			if(global_debug) \
				fprintf(stderr,"state=%d\n",state); \
			info->ccm_node_state = state; \
			if(state==CCM_STATE_JOINING) \
				client_not_primary(); \
		}


#define 	CCM_SET_JOINED_TRANSITION(info, trans) \
					info->ccm_joined_transition = trans
#define 	CCM_SET_COOKIE(info, val) \
				strncpy(info->ccm_cookie, val, COOKIESIZE)
#define 	CCM_SET_GRAPH(info, graph)  	info->ccm_graph = graph
#define 	CCM_SET_CL(info, index)	info->ccm_cluster_leader = index
#define 	CCM_SET_JOINERHEAD(info, ptr)	info->ccm_joiner_head = ptr


#define		CCM_GET_ACTIVEPROTO(info) info->ccm_active_proto
#define		CCM_GET_MAJORTRANS(info) info->ccm_transition_major
#define		CCM_GET_MINORTRANS(info) info->ccm_transition_minor
#define		CCM_GET_STATE(info) 	info->ccm_node_state 
#define		CCM_GET_HIPROTO(info) 	info->ccm_hiproto 
#define 	CCM_GET_LLM(info) 	(&(info->ccm_llm))
#define 	CCM_GET_UPDATETABLE(info) (&(info->ccm_update))
#define 	CCM_GET_GRAPH(info)  	info->ccm_graph
#define 	CCM_GET_JOINED_TRANSITION(info) info->ccm_joined_transition
#define  	CCM_GET_LLM_NODECOUNT(info) LLM_GET_NODECOUNT(CCM_GET_LLM(info))
#define  	CCM_GET_MY_HOSTNAME(info)  ccm_get_my_hostname(info)
#define 	CCM_GET_COOKIE(info) info->ccm_cookie

#define 	CCM_RESET_MEMBERSHIP(info)  info->ccm_nodeCount=0
#define 	CCM_ADD_MEMBERSHIP(info, index)  \
				info->ccm_member[info->ccm_nodeCount++] = index
#define 	CCM_GET_MEMCOUNT(info)  info->ccm_nodeCount
#define 	CCM_GET_MEMINDEX(info, i)	info->ccm_member[i]
#define 	CCM_GET_MEMTABLE(info)		info->ccm_member
#define 	CCM_GET_CL(info)  		info->ccm_cluster_leader
#define 	CCM_GET_JOINERHEAD(info)	info->ccm_joiner_head
#define		CCM_TRANS_EARLIER(trans1, trans2) (trans1 < trans2) /*TOBEDONE*/
#define 	CCM_GET_VERSION(info)	&(info->ccm_version)



/* PROTOTYPE */
static void ccm_send_join_reply(ll_cluster_t *, ccm_info_t *);
static int ccm_send_final_memlist(ll_cluster_t *, ccm_info_t *, char *, char *);
static void report_reset(void);
static int ccm_already_joined(ccm_info_t *);


////////////////////////////////////////////////////////////////
// BEGIN OF Functions associated with CCM token types that are
// communicated accross nodes and their values.
////////////////////////////////////////////////////////////////


// the ccm types tokens used locally, these are the integer equivalents
// for the F_TYPE tokens. The strings defined in ccm_type_str are
// communicated accross the wire. But locally they are mapped to
// ccm_types for easier processing.
enum ccm_type {
	CCM_TYPE_PROTOVERSION=1,
	CCM_TYPE_PROTOVERSION_RESP,
	CCM_TYPE_JOIN,
	CCM_TYPE_REQ_MEMLIST,
	CCM_TYPE_RES_MEMLIST,
	CCM_TYPE_FINAL_MEMLIST,
	CCM_TYPE_ABORT,
	CCM_TYPE_LEAVE,
	CCM_TYPE_TIMEOUT,
	CCM_TYPE_ERROR,
	CCM_TYPE_LAST
};

// the ccm strings tokens communicated aross the wire.
// these are the values for the F_TYPE names.
#define TYPESTRSIZE 20
char  ccm_type_str[CCM_TYPE_LAST][TYPESTRSIZE] = {
			"",
			"ccmpver",
			"ccmpverresp",
			"ccmjoin",
			"ccmreqmlst",
			"ccmresmlst",
			"ccmfnlmlst",
			"ccmabrt",
			"ccmlv",
			"ccmtmout",
			""
	};

//
// ccm defined new type tokens used by the CCM protocol.
//
#define CCM_VERSIONVAL  "ccmpverval" 	  /* version value token */
#define CCM_UPTIME      "ccmuptime"       /* Uptime for Consensus  */
#define CCM_MEMLIST     "ccmmemlist"      /* bitmap for membership */
#define CCM_PROTOCOL    "ccmproto"        /* protocol version */
#define CCM_MAJORTRANS  "ccmmajor"        /* major transition version*/
#define CCM_MINORTRANS  "ccmminor"        /* minor transition version */
#define CCM_COOKIE      "ccmcookie"       /* communication context */
#define CCM_NEWCOOKIE   "ccmnewcookie"    /* new communication context */
#define CCM_CLSIZE   	"ccmclsize"       /* new cluster size */


// given a ccm_type return the string representation associated with it.
// NOTE: string representation is used to communicate accross node.
//       and ccm_type is used for easier local processing.
static char *
ccm_type2string(enum ccm_type type)
{
	return ccm_type_str[type];
}

//
// given a string representation return the string type.
//
static enum ccm_type 
ccm_string2type(const char *type)
{
	enum ccm_type i;

	for ( i = CCM_TYPE_PROTOVERSION; i <= CCM_TYPE_LAST; i++ ) {
		if (strncmp(ccm_type_str[i], type, TYPESTRSIZE) == 0)
			return i;
	}
	return CCM_TYPE_ERROR;
}

// END OF TYPE_STR datastructure and associated functions



//
// ccm_get_my_hostname: return my nodename.
//
static char *
ccm_get_my_hostname(ccm_info_t *info)
{
	llm_info_t *llm = CCM_GET_LLM(info);
	return(LLM_GET_MYNODEID(llm));
}


//
// timeout_msg_create: 
//	fake up a timeout message, which is in the
// 	same format as the other messages that are
//	communicated across the nodes.
//

static struct ha_msg * timeout_msg = NULL;



static int
timeout_msg_init(ccm_info_t *info)
{
	struct ha_msg *m;
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/
	char *hname;

	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send CCM version msg");
		return(HA_FAIL);
	}

	hname = ccm_get_my_hostname(info);

	snprintf(majortrans, 15, "%d", 0);
	snprintf(minortrans, 15, "%d", 0);
	if((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_TIMEOUT)) == HA_FAIL)
		||(ha_msg_add(m, F_ORIG, hname) == HA_FAIL) 
		||(ha_msg_add(m, CCM_COOKIE, "  ") == HA_FAIL) 
		||(ha_msg_add(m, CCM_COOKIE, "  ") == HA_FAIL) 
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)){
			ha_log(LOG_ERR, "timeout_msg_create: Cannot "
				"create timeout message");
		return HA_FAIL;
	}
	timeout_msg = m;
	return 0;
}

static struct ha_msg  *
timeout_msg_mod(ccm_info_t *info)
{
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/

	char *cookie = CCM_GET_COOKIE(info);

	int major  = CCM_GET_MAJORTRANS(info);
	int minor  = CCM_GET_MINORTRANS(info);

	struct ha_msg *m = timeout_msg;
	assert(m);
	snprintf(majortrans, 15, "%d", major);
	snprintf(minortrans, 15, "%d", minor);
	if((ha_msg_mod(m, CCM_COOKIE, cookie) == HA_FAIL)
		||(ha_msg_mod(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_mod(m, CCM_MINORTRANS, minortrans) == HA_FAIL)){
			ha_log(LOG_ERR, "timeout_msg_mod: Cannot "
				"modify timeout message");
		return NULL;
	}
	return m;
}


#ifdef TIMEOUT_MSG_FUNCTIONS_NEEDED
//
// timeout_msg_done: 
//   done with the processing of this message.
static void
timeout_msg_done(void)
{
	// nothing to do.
	return;
}


//
// timeout_msg_del: 
//   delete the given timeout message.
//   nobody calls this function. 
//   someday somebody will call it :)
static void
timeout_msg_del(void)
{
	ha_msg_del(timeout_msg);
	timeout_msg = NULL;
}
#endif


//
// These are the function that keep track of number of time a version
// response message has been dropped. These function are consulted by
// the CCM algorithm to determine if a version response message has
// to be dropped or not.
//
static int respdrop=0;
#define MAXDROP 3

static int
resp_can_i_drop(void)
{
	if (respdrop >= MAXDROP)
		return FALSE;
	return TRUE;
}

static void
resp_dropped(void)
{
	respdrop++;
}

static void
resp_reset(void)
{
	respdrop=0;
}
//
// End of response processing messages.
//


//
// BEGIN OF functions that track the time since a connectivity reply has
// been sent to the leader.
//
static struct  timeval finallist_time;

static void
finallist_init(void)
{
	ccm_get_time(&finallist_time);
}

static void
finallist_reset(void)
{
	bzero(&finallist_time, sizeof(struct timeval));
}

static int
finallist_timeout(long timeout)
{
	struct timeval tmp;

	ccm_get_time(&tmp);

	return(ccm_timeout(&finallist_time, &tmp, timeout));
}
//
// END OF functions that track the time since a connectivity reply has
// been sent to the leader.
//


// BEGINE of the functions that track asynchronous leave
//
// When ccm running on a  node leaves the cluster voluntarily it 
// sends  a  leave  message  to  the  other nodes in the cluster. 
// Similarly  whenever  ccm  running on some node of the cluster,
// dies  the  local  heartbeat   delivers a leave message to ccm.
// And  whenever  some node in the cluster dies, local heartbeat 
// informs  the  death  through  a  callback. 
// In all these cases, ccm is informed about the loss of the node,
// asynchronously, in  some context where immidiate processing of 
// the message is not possible. 
// The  following  set of routines act as a cache that keep track 
// of  message  leaves  and  facilitates  the  delivery  of these 
// messages at a convinient time.
// 
//
static unsigned char *leave_bitmap=NULL;

static void
leave_init(void)
{
	int numBytes;

	assert(!leave_bitmap);
	numBytes = bitmap_create(&leave_bitmap, MAXNODE);
	bzero(leave_bitmap, numBytes);
}

static void
leave_reset(void)
{
	int numBytes = bitmap_size(MAXNODE);
	if(!leave_bitmap) return;
	bzero(leave_bitmap, numBytes);
	return;
}

static void
leave_cache(int i)
{
	assert(leave_bitmap);
	bitmap_mark(i, leave_bitmap, MAXNODE);
}

static int
leave_get_next(void)
{
	int i;

	assert(leave_bitmap);
	for ( i = 0 ; i < MAXNODE; i++ ) {
		if(bitmap_test(i,leave_bitmap,MAXNODE)) {
			bitmap_clear(i,leave_bitmap,MAXNODE);
			return i;
		}
	}
	return -1;
}

static int
leave_any(void)
{
	if(bitmap_count(leave_bitmap,MAXNODE)) return TRUE;
	return FALSE;
}
/* leave bitmap relate routines end */




// Reset all the datastructures. Go to a state which is equivalent
// to a state when the node is just about to join a cluster.
static void 
ccm_reset(ccm_info_t *info)
{

	if(ccm_already_joined(info)) client_evicted();

	CCM_RESET_MEMBERSHIP(info);
	graph_free(CCM_GET_GRAPH(info));
	CCM_SET_GRAPH(info,NULL);
	CCM_SET_ACTIVEPROTO(info, CCM_VER_NONE);
	CCM_SET_COOKIE(info,"");
	CCM_SET_MAJORTRANS(info,0);
	CCM_SET_MINORTRANS(info,0);
	CCM_SET_CL(info,-1);
	CCM_SET_JOINED_TRANSITION(info, 0);
	CCM_SET_STATE(info, CCM_STATE_NONE);
	update_reset(CCM_GET_UPDATETABLE(info));
	g_slist_free(CCM_GET_JOINERHEAD(info));
	CCM_SET_JOINERHEAD(info, NULL);
	version_reset(CCM_GET_VERSION(info));
	finallist_reset();
	leave_reset();
	report_reset();
}

static void 
ccm_init(ccm_info_t *info)
{
	CCM_SET_GRAPH(info,NULL);
	update_init(CCM_GET_UPDATETABLE(info));
	CCM_SET_JOINERHEAD(info, NULL);
        leave_init();
        (void)timeout_msg_init(info);
	ccm_reset(info);
}


/* a sophisticated quorum algorithm has to be introduced here
 *  currently we are just using the simplest algorithm
 */
static int
ccm_quorum(ccm_info_t *info)
{
	if(CCM_GET_MEMCOUNT(info)< 
		(LLM_GET_NODECOUNT(CCM_GET_LLM(info))/2+1))
		return 0;
	return 1;
}

/*
 * BEGIN OF ROUTINES THAT REPORT THE MEMBERSHIP TO CLIENTS.
 */

static int    old_mem[MAXNODE]; /*avoid making it a stack variable*/
static int    old_size=0;


static int
mbr_compare(const void *value1, const void *value2)
{
	const int *t1 = (const int *)value1;
	const int *t2 = (const int *)value2;
	if(*t1>*t2) return 1;
	if(*t1<*t2) return -1;
	return 0;
}

static int
report_mbr_compare(int *newtable, 
		int newsize, 
		int *oldtable, 
		int oldsize)
{
	if(newsize!=oldsize) return 1;
	qsort(newtable, newsize, sizeof(int), mbr_compare);
	return memcmp(newtable,oldtable,newsize*sizeof(int));
}

static void
report_mbr_copy(int *newtable, 
		int newsize, 
		int *oldtable, 
		int *oldsize)
{
	memcpy(oldtable,newtable,newsize*sizeof(int));
	*oldsize=newsize;
	return;
}


static void
report_reset(void)
{
	old_size=0;
}
//
// print the membership of the cluster.
//
static void
report_mbrs(ccm_info_t *info)
{
	int i;
	char *nodename;

	static struct born_s  {
		int index;
		int bornon;
	}  bornon[MAXNODE];/*avoid making it a 
						stack variable*/
	

	if(CCM_GET_MEMCOUNT(info)==1){
		bornon[0].index  = CCM_GET_MEMINDEX(info,0);
		bornon[0].bornon = CCM_GET_MAJORTRANS(info);
	} else for(i=0; i < CCM_GET_MEMCOUNT(info); i++){
		bornon[i].index = CCM_GET_MEMINDEX(info,i);
		bornon[i].bornon = update_get_uptime(CCM_GET_UPDATETABLE(info), 
				CCM_GET_LLM(info),
				CCM_GET_MEMINDEX(info,i));
		if(bornon[i].bornon==0) 
			bornon[i].bornon=CCM_GET_MAJORTRANS(info);
		assert(bornon[i].bornon!=-1);
	}

	if(global_verbose) {
		fprintf(stderr,"\t\t the following are the members " 
			"of the group of transition=%d\n",
			CCM_GET_MAJORTRANS(info));

		for (i=0 ;  i < CCM_GET_MEMCOUNT(info); i++) {
			nodename = LLM_GET_NODEID(CCM_GET_LLM(info), 
					CCM_GET_MEMINDEX(info,i));
			fprintf(stderr,"\t\tnodename=%s bornon=%d\n", nodename, 
					bornon[i].bornon);
		}
	}

	if(ccm_quorum(info)){
		if(report_mbr_compare(CCM_GET_MEMTABLE(info), 
					CCM_GET_MEMCOUNT(info),
					old_mem, old_size)==0){
			client_primary_restored();
		} else {
			client_send_msg(CCM_GET_MEMCOUNT(info), 
				CCM_GET_MAJORTRANS(info),
				CCM_GET_MEMTABLE(info), 
				bornon);
			report_mbr_copy(CCM_GET_MEMTABLE(info), 
					CCM_GET_MEMCOUNT(info), 
					old_mem, &old_size);
		}
	}
	return;
}
/*
 * END OF ROUTINES THAT REPORT THE MEMBERSHIP TO CLIENTS.
 */




//
// generate a random cookie.
// NOTE: cookie  is  a  mechanism  of  seperating out the contexts
// of  messages  of  partially  partitioned  clusters.
// For example, consider  a  case  where   node  A  is  physically
// in  the  partition  X  and  partition  Y,  and  but  has joined 
// membership  in partition X. It will end up getting ccm protocol
// messages  sent  by  members in both the partitions. In order to 
// seperate  out  messages  belonging  to  individual partition, a 
// random  string  is  used  as  a identifier by each partition to 
// identify  its  messages.  In  the above case A will get message 
// from  both  the  partitions  but  only listens to messages from 
// partition X and drops messages from partition Y.
//
static char *
ccm_generate_random_cookie(void)
{
	char *cookie;
	int i;
	struct timeval tmp;

	cookie = g_malloc(COOKIESIZE*sizeof(char));
	assert(cookie);

	/* seed the random with a random value */
	gettimeofday(&tmp, NULL);
	srandom((unsigned int)tmp.tv_usec); 

	for ( i = 0 ; i < COOKIESIZE-1; i++ ) {
		cookie[i] = random()%(127-'!')+'!';
	}
	cookie[i] = '\0';
	return cookie;
}


static void
ccm_free_random_cookie(char *cookie)
{
	assert(cookie);
	g_free(cookie);
}



// BEGIN OF FUNCTIONS that keep track of connectivity  information 
// conveyed by individual members of the cluster. These  functions 
// are used by only the cluster leader. Ultimately these connectivity
// information is used by the cluster to extract out the members
// of the cluster that have total connectivity.
static int
ccm_membership_already_noted(ccm_info_t *info, const char *orig)
{
	/* find the uuid of the originator */
	int uuid = llm_get_uuid(CCM_GET_LLM(info), orig);
	return graph_membership_already_noted(CCM_GET_GRAPH(info), uuid);
}

static void
ccm_modify_membership(ccm_info_t *info, const char *orig, const char *memlist)
{
	int numbytes;
	unsigned char *bitlist;

	int uuid = llm_get_uuid(CCM_GET_LLM(info), orig);

	graph_delete_membership(CCM_GET_GRAPH(info), uuid);

	/* convert the memlist into a bit map and feed it to the graph */
	numbytes = ccm_str2bitmap(memlist, &bitlist);
	
	graph_update_membership(CCM_GET_GRAPH(info), uuid, bitlist);
	/*NOTE DO NOT DELETE bitlist, because it is being handled by graph*/
}

static void
ccm_note_membership(ccm_info_t *info, const char *orig, const char *memlist)
{
	int uuid, numbytes;
	unsigned char *bitlist;

	/* find the uuid of the originator */
	uuid = llm_get_uuid(CCM_GET_LLM(info), orig);

	/* convert the memlist into a bit map and feed it to the graph */
	numbytes = ccm_str2bitmap(memlist, &bitlist);
	
	graph_update_membership(CCM_GET_GRAPH(info), uuid, bitlist);
	/*NOTE DO NOT DELETE bitlist, because it is being handled by graph*/
}

// called by the cluster leader only 
static void
ccm_note_my_membership(ccm_info_t *info)
{
	int uuid, numbytes;
	unsigned char *bitlist;
	char *memlist;
	int str_len;

	/* find the uuid of the originator */
	uuid = llm_get_uuid(CCM_GET_LLM(info), ccm_get_my_hostname(info));
	str_len = update_strcreate(CCM_GET_UPDATETABLE(info), 
					&memlist, CCM_GET_LLM(info));
	/* convert the memlist into a bit map and feed it to the graph */
	numbytes = ccm_str2bitmap(memlist, &bitlist);
	update_strdelete(memlist);
	graph_update_membership(CCM_GET_GRAPH(info), uuid, bitlist);
	/*NOTE DO NOT DELETE bitlist, because it is being handled by graph*/
	return;
}

/* add a new member to the membership list */
static void
ccm_add_membership(ccm_info_t *info, const char *orig)
{
	int uuid, myuuid;

	uuid = llm_get_uuid(CCM_GET_LLM(info), orig);
	myuuid = llm_get_uuid(CCM_GET_LLM(info), ccm_get_my_hostname(info));
	graph_add_uuid(CCM_GET_GRAPH(info), uuid);
	graph_add_to_membership(CCM_GET_GRAPH(info), myuuid, uuid);
	return;
}

static void 
ccm_membership_init(ccm_info_t *info)
{
	int track=-1;
	int uuid;
	

	CCM_SET_GRAPH(info, graph_init());

	/* go through the update list and note down all the members who
	 * had participated in the join messages. We should be expecting
	 * reply memlist bitmaps atleast from these nodes.
	 */
	while((uuid = update_get_next_uuid(CCM_GET_UPDATETABLE(info), 
				CCM_GET_LLM(info), &track)) != -1) {
		graph_add_uuid(CCM_GET_GRAPH(info),uuid); 
	}
}

static void 
ccm_membership_free(ccm_info_t *info)
{
	graph_free(CCM_GET_GRAPH(info));
	CCM_SET_GRAPH(info,NULL);
}

static int
ccm_rcvd_all_memlist(ccm_info_t *info)
{
	return graph_filled_all(CCM_GET_GRAPH(info));
}



static int
ccm_membership_timeout(ccm_info_t *info, long timeout)
{
	return graph_timeout_expired(CCM_GET_GRAPH(info), 
		timeout);
}


static int 
ccm_am_i_member(ccm_info_t *info, const char *memlist)
{
	unsigned char *bitmap;

	int numBytes = ccm_str2bitmap(memlist, &bitmap);

	/* what is my node Uuid */
	llm_info_t *llm = CCM_GET_LLM(info);

	int my_uuid = LLM_GET_MYUUID(llm);

	if (bitmap_test(my_uuid, bitmap, numBytes)){
		bitmap_delete(bitmap);
		return TRUE;
	}

	bitmap_delete(bitmap);
	return FALSE;
}
//
// END OF the membership tracking functions.
//



//
// BEGIN  OF  FUNCTIONS  that  keep track of stablized membership list
// 
// These  function  keep track of consensus membership once a instance
// of the  ccm algorithm terminates and decided on the final consensus 
// members of the cluster.
//
static int 
ccm_memlist_changed(ccm_info_t *info, 
		  char *bitmap /* the bitmap string containing bits */)
{
	int nodeCount, i;
	llm_info_t *llm;
	int indx, uuid;
		
		
	/* go through the membership list */
	nodeCount = CCM_GET_MEMCOUNT(info);
	llm = CCM_GET_LLM(info);
	for ( i = 0 ; i < nodeCount; i++ ) {
		indx = CCM_GET_MEMINDEX(info, i);
		assert(indx >=0 && indx < LLM_GET_NODECOUNT(llm));
		uuid = LLM_GET_UUID(llm,indx);
		assert(uuid>=0 && uuid < MAXNODE);
		if (!bitmap_test(uuid, bitmap, MAXNODE))
			return TRUE;
	}
	return FALSE;
} 

static int 
ccm_fill_memlist(ccm_info_t *info, 
	const unsigned char *bitmap)
{
	llm_info_t *llm;
	int i, uuid;

	llm = CCM_GET_LLM(info);
	CCM_RESET_MEMBERSHIP(info);
	for ( i = 0 ; i < LLM_GET_NODECOUNT(llm); i++ ) {
		uuid = LLM_GET_UUID(llm,i);
		if(bitmap_test(uuid, bitmap, MAXNODE)){
			/*update the membership list with this member*/
			CCM_ADD_MEMBERSHIP(info, i);
		}
	}

	return FALSE;
}

static int 
ccm_fill_memlist_from_str(ccm_info_t *info, 
	const unsigned char *memlist)
{
	unsigned char *bitmap;
	int ret;

	(void)ccm_str2bitmap(memlist, &bitmap);
	ret = ccm_fill_memlist(info, bitmap);
	bitmap_delete(bitmap);
	return ret;
}

									
static int 
ccm_fill_memlist_from_bitmap(ccm_info_t *info, 
	const unsigned char *bitmap)
{
	return ccm_fill_memlist(info, bitmap);
}






static int
ccm_get_membership_index(ccm_info_t *info, const char *node)
{
	int i,indx;
	llm_info_t *llm = CCM_GET_LLM(info);
	for ( i = 0 ; i < CCM_GET_MEMCOUNT(info) ; i++ ) {
		indx =  CCM_GET_MEMINDEX(info, i);
		if(strncmp(LLM_GET_NODEID(llm, indx), node, 
				LLM_GET_NODEIDSIZE(llm)) == 0)
			return i;
	}
	return -1;
}


static int
ccm_get_my_membership_index(ccm_info_t *info)
{
	int i;
	llm_info_t *llm = CCM_GET_LLM(info);

	for ( i = 0 ; i < CCM_GET_MEMCOUNT(info) ; i++ ) {
		if (CCM_GET_MEMINDEX(info, i) == LLM_GET_MYNODE(llm))
			return i;
	}
		
	assert(0); /* should never reach here */
	return -1;
}

static int
ccm_am_i_leader(ccm_info_t *info)
{
	if (ccm_get_my_membership_index(info) == CCM_GET_CL(info))
		return TRUE;
	return FALSE;
}

static int
ccm_already_joined(ccm_info_t *info)
{
	if (CCM_GET_JOINED_TRANSITION(info)) {
		return TRUE;
	}
	return FALSE;
}

//
// END  OF  FUNCTIONS  that  keep track of stablized membership list
//


// 
// BEGIN OF FUNCTIONS THAT KEEP TRACK of cluster nodes that have shown
// interest in joining the cluster.
//
//
// NOTE: when a new node wants to join the cluster, it multicasts a 
// message asking for the necessary information to send out a  join
// message. (it needs the current major transistion number, the context
// string i.e cookie, the protocol number that everybody is operating
// in).
//
// The functions below track these messages sent out by new potential
// members showing interest in acquiring the initial context.
//
static void 
ccm_add_new_joiner(ccm_info_t *info, const char *orig)
{
	/* check if there is already a cached request for the
	 * joiner 
	 */
	int idx = llm_get_index(CCM_GET_LLM(info), orig)+1;
	if(!g_slist_find(CCM_GET_JOINERHEAD(info),GINT_TO_POINTER(idx))) {
		CCM_SET_JOINERHEAD(info, g_slist_append(CCM_GET_JOINERHEAD(info), 
					GINT_TO_POINTER(idx)));
	} 
	else {
		if(global_debug)
		fprintf(stderr,"add_joiner %s already done\n", orig);
	}
	return;
}


static int
ccm_am_i_highest_joiner(ccm_info_t *info)
{
	int   		joiner;
	char *		joiner_name;
	gpointer	jptr;
	char *		hname = ccm_get_my_hostname(info);
	int  		i=0;

	/* FIXME!  g_slist_nth_data is a slow operation ! */
	/* The way it's used in this loop makes it an n**2 operation! */

	while( (jptr=g_slist_nth_data(CCM_GET_JOINERHEAD(info),i++)) != NULL){
		joiner = GPOINTER_TO_INT(jptr)-1;
		joiner_name = LLM_GET_NODEID(CCM_GET_LLM(info), joiner);
		if (strncmp(hname, joiner_name, 
			LLM_GET_NODEIDSIZE(CCM_GET_LLM(info))) < 0) {
			return FALSE;
		}
	}
	return TRUE;
}

static void 
ccm_remove_new_joiner(ccm_info_t *info, const char *orig)
{
	int idx = llm_get_index(CCM_GET_LLM(info), orig)+1;
	g_slist_remove(CCM_GET_JOINERHEAD(info), (gpointer)idx);
	return;
}
//
// END OF FUNCTIONS THAT KEEP TRACK of cluster nodes that have shown
// interest in joining the cluster.
//


/////////////////////////////////////////////////////////////////////
//
// BEGIN OF FUNCTIONS THAT SEND OUT messages to nodes of the cluster
//
/////////////////////////////////////////////////////////////////////
static void
ccm_delay_random_interval(void)
{
	struct timeval tmp;
	/* seed the random with a random value */
	gettimeofday(&tmp, NULL);
	srandom((unsigned int)tmp.tv_usec); 
 	usleep(random()%MAXNODE); /*sleep some random microsecond interval*/
}

//
// compute the final membership list from the acquired connectivity
// information from other nodes. And send out the consolidated
// members of the cluster information to the all the members of 
// that have participated in the CCM protocol.
//
// NOTE: Called by the cluster leader only.
//
static void
ccm_compute_and_send_final_memlist(ll_cluster_t *hb, ccm_info_t *info)
{
	unsigned char *bitmap;
	int count;
	char *string;
	char *cookie = NULL;
	int numBytes;
	int strsize;

	/* get the maxmimum membership list */
	count = graph_get_maxclique(CCM_GET_GRAPH(info), &bitmap);

	/* create a string with the membership information */
	numBytes = bitmap_size(MAXNODE);
	strsize  = ccm_bitmap2str(bitmap, numBytes, &string);


	/* check if the membership has changed from that before. If so we
	 * have to generate a new cookie.
 	 */
	if(ccm_memlist_changed(info, bitmap)) {
		cookie = ccm_generate_random_cookie();
	}

	while (ccm_send_final_memlist(hb, info, cookie, string) != HA_OK) {
		ha_log(LOG_ERR, "ccm_compute_and_send_final_memlist: failure "
						"to send finalmemlist");
		sleep(1);
	}

	/* fill my new memlist and update the new cookie if any */
	ccm_fill_memlist_from_bitmap(info, bitmap);
	bitmap_delete(bitmap);
	g_free(string);

	/* increment the major transition number and reset the
	 * minor transition number
	 */
	CCM_INCREMENT_MAJORTRANS(info); 
	CCM_RESET_MINORTRANS(info);

	/* if cookie has changed update it.
	 */
	if (cookie) {
		ha_log(LOG_INFO, "ccm_compute_and_send_final_list: "
				"cookie changed ");
		CCM_SET_COOKIE(info, cookie); 
		ccm_free_random_cookie(cookie);
	}

	/* check if any joiner is waiting for a response from us. 
	 * If so respond 
	 */
	ccm_send_join_reply(hb, info);
	
	CCM_SET_CL(info, ccm_get_my_membership_index(info));
	report_mbrs(info);/* call this before update_reset() */
	update_reset(CCM_GET_UPDATETABLE(info));
	ccm_membership_free(info);
	CCM_SET_STATE(info, CCM_STATE_JOINED);
	if(!ccm_already_joined(info)) 
		CCM_SET_JOINED_TRANSITION(info, CCM_GET_MAJORTRANS(info));
	return;
}



//
// send a reply to the potential joiner, containing the neccessary
// context needed by the joiner, to initiate a new round of a ccm 
// protocol.
// NOTE: This function is called by the cluster leader only.
//
static int 
ccm_send_joiner_reply(ll_cluster_t *hb, ccm_info_t *info, const char *joiner)
{
	struct ha_msg *m;
	char activeproto[3];
	char clsize[5];
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char *cookie;
	int rc;


	/*send the membership information to all the nodes of the cluster*/
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send CCM version msg");
			return(HA_FAIL);
	}
	
	snprintf(activeproto, sizeof(activeproto), "%d", 
			CCM_GET_ACTIVEPROTO(info));
	snprintf(majortrans, sizeof(majortrans), "%d", 
				CCM_GET_MAJORTRANS(info));
	snprintf(clsize, sizeof(clsize), "%d", 
				CCM_GET_MEMCOUNT(info));
	cookie = CCM_GET_COOKIE(info);
	assert(cookie);

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_PROTOVERSION_RESP)) 
					== HA_FAIL)
		||(ha_msg_add(m, CCM_PROTOCOL, activeproto) == HA_FAIL) 
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_CLSIZE, clsize) == HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL)) {
		ha_log(LOG_ERR, "ccm_send_joiner_reply: Cannot create JOIN "
				"reply message");
			rc = HA_FAIL;
		} else {
			rc = hb->llc_ops->sendnodemsg(hb, m, joiner);
		}
	ha_msg_del(m);
	return(rc);
}

// 
// browse through the list of interested joiners and reply to each of
// them.
// 
static void 
ccm_send_join_reply(ll_cluster_t *hb, ccm_info_t *info)
{
	int 	joiner;
	gpointer	jptr;
	const char *joiner_name;
	GSList 	*tmphead;
	int	i=0;

	tmphead = CCM_GET_JOINERHEAD(info);

	/* FIXME!  g_slist_nth_data is a slow operation ! */
	/* The way it's used in this loop makes it an n**2 operation! */

	while( (jptr=g_slist_nth_data(CCM_GET_JOINERHEAD(info),i++)) != NULL){
		joiner = GPOINTER_TO_INT(jptr)-1;
		joiner_name = LLM_GET_NODEID(CCM_GET_LLM(info), joiner);
		/* send joiner the neccessary information */
		while (ccm_send_joiner_reply(hb, info, joiner_name)!=HA_OK) {
			ha_log(LOG_ERR, "ccm_send_join_reply: failure "
				"to send join reply");
			sleep(1);
		}
	}
}


//
// send a final membership list to all the members who have participated
// in the ccm protocol.
// NOTE: Called by the cluster leader on.
//
static int
ccm_send_final_memlist(ll_cluster_t *hb, 
			ccm_info_t *info, 
			char *newcookie, 
			char *finallist)
{  
	struct ha_msg *m;
	char activeproto[3];
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char *cookie;
	int rc;


	/*send the membership information to all the nodes of the cluster*/
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send CCM version msg");
		return(HA_FAIL);
	}
	
	snprintf(activeproto, sizeof(activeproto), "%d", 
					CCM_GET_ACTIVEPROTO(info));
	snprintf(majortrans, sizeof(majortrans), "%d", 
					CCM_GET_MAJORTRANS(info));
	snprintf(minortrans, sizeof(minortrans), "%d", 
					CCM_GET_MINORTRANS(info));
	cookie = CCM_GET_COOKIE(info);

	assert(cookie);
	assert(finallist);

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_FINAL_MEMLIST)) 
							== HA_FAIL)
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL)
		||(ha_msg_add(m, CCM_MEMLIST, finallist) == HA_FAIL)
		||(!newcookie? FALSE: (ha_msg_add(m, CCM_NEWCOOKIE, newcookie)
							==HA_FAIL))) {
		ha_log(LOG_ERR, "ccm_send_final_memlist: Cannot create "
					"FINAL_MEMLIST message");
		rc = HA_FAIL;
	} else {
		rc = hb->llc_ops->sendclustermsg(hb, m);
	}
	ha_msg_del(m);
	return(rc);
}



//
// send out a message to the cluster asking for the context
// NOTE: this context is used to intiate a new instance of 
// 	a CCM protocol.
//
static int
ccm_send_protoversion(ll_cluster_t *hb, ccm_info_t *info)
{
	struct ha_msg *m;
	char version[3]; /* in the life time of ccm, do not expect protocol
					    versions running to 100! */
	int  rc;
	
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send CCM version msg");
		return(HA_FAIL);
	}
	
	snprintf(version, sizeof(version), "%d",  CCM_GET_HIPROTO(info));

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_PROTOVERSION)) 
							== HA_FAIL)) {
		ha_log(LOG_ERR, "ccm_send_join: Cannot create PROTOVERSION "
						    "message");
		rc = HA_FAIL;
	} else {		
		rc = hb->llc_ops->sendclustermsg(hb, m);
	}
	ha_msg_del(m);
	return(rc);
}

//
// send out a abort message to whoever has initiated a new instance
// of ccm protocol.
//
static int
ccm_send_abort(ll_cluster_t *hb, ccm_info_t *info, 
		const char *dest, 
		const int major, 
		const int minor)
{
	struct ha_msg *m;
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/
	char *cookie;
	int  rc;

	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send CCM version msg");
		return(HA_FAIL);
	}
	
	snprintf(majortrans, 15, "%d", major);
	snprintf(minortrans, 15, "%d", minor);
	cookie = CCM_GET_COOKIE(info);
	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_ABORT)) == HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL) 
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)){
			ha_log(LOG_ERR, "ccm_send_abort: Cannot create ABORT "
						    "message");
		rc = HA_FAIL;
	} else {
		rc = hb->llc_ops->sendnodemsg(hb, m, dest);
	}
	ha_msg_del(m);
	return(rc);
}



//
// send out a leave message to indicate to everybody that it is leaving
// the cluster.
//
static int
ccm_send_leave(ll_cluster_t *hb, ccm_info_t *info)
{
	struct ha_msg *m;
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/
	char *cookie;
	int  rc;

	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send CCM version msg");
		return(HA_FAIL);
	}
	
	snprintf(majortrans, sizeof(majortrans), "%d", 
				CCM_GET_MAJORTRANS(info));
	snprintf(minortrans, sizeof(minortrans), "%d", 
				CCM_GET_MINORTRANS(info));
	cookie = CCM_GET_COOKIE(info);
	assert(cookie);

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_LEAVE)) == HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL)
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)){
			ha_log(LOG_ERR, "ccm_send_leave: Cannot create leave "
						    "message");
		rc = HA_FAIL;
	} else {
		rc = hb->llc_ops->sendclustermsg(hb, m);
	}
	ha_msg_del(m);
	return(rc);
}

//
// send out a join message. THis message will initiate a new instance of
// the ccm protocol.
//
static int
ccm_send_join(ll_cluster_t *hb, ccm_info_t *info)
{
	struct ha_msg *m;
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/
	char joinedtrans[15]; /*		ditto 	*/
	int  joinedtrans_val;
	char *cookie;
	int  rc;

	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "Cannot send CCM version msg");
		return(HA_FAIL);
	}
	
	snprintf(majortrans, sizeof(majortrans), "%d", 
				CCM_GET_MAJORTRANS(info));
	snprintf(minortrans, sizeof(minortrans), "%d", 
				CCM_GET_MINORTRANS(info));
	/* uptime is based on the transition during which a given node
	 * officially joined the cluster 
	 */
	cookie = CCM_GET_COOKIE(info);
	assert(cookie);

	joinedtrans_val = CCM_GET_JOINED_TRANSITION(info);
	joinedtrans_val = (joinedtrans_val == -1)? 0: joinedtrans_val;
	snprintf(joinedtrans, sizeof(joinedtrans_val), "%d", joinedtrans_val);

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_JOIN)) == HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL)
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_UPTIME, joinedtrans) == HA_FAIL)) {
			ha_log(LOG_ERR, "ccm_send_join: Cannot create JOIN "
						    "message");
		rc = HA_FAIL;
	} else {
		/*delay by microseconds to avoid message collision */
		ccm_delay_random_interval();
		rc = hb->llc_ops->sendclustermsg(hb, m);
	}
	ha_msg_del(m);
	return(rc);
}


//
// send out the connectivity information to the cluster leader.
//
static int
ccm_send_memlist_res(ll_cluster_t *hb, 
			ccm_info_t *info,
			const char *nodename, 
			char *memlist)
{
	struct ha_msg *m;
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/
	char *cookie;
	int  rc;
	unsigned char *bitmap;
	int del_flag=0;
	
	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "ccm_send_memlist_res: Cannot allocate "
				"memory ");
		return(HA_FAIL);
	}
	
	snprintf(majortrans, sizeof(majortrans), "%d", 
					CCM_GET_MAJORTRANS(info));
	snprintf(minortrans, sizeof(minortrans), "%d", 
					CCM_GET_MINORTRANS(info));

	cookie = CCM_GET_COOKIE(info);
	assert(cookie);

	if (!memlist) {
		int numBytes = bitmap_create(&bitmap, MAXNODE);
		(void) ccm_bitmap2str(bitmap, numBytes, &memlist);
		bitmap_delete(bitmap);
		del_flag = 1;
	} 

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_RES_MEMLIST)) 
							== HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL)
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MEMLIST, memlist) == HA_FAIL)) {
		ha_log(LOG_ERR, "ccm_send_memlist_res: Cannot create "
						"RES_MEMLIST message");
		rc = HA_FAIL;
	} else {
		/*delay by microseconds to avoid message collision */
		ccm_delay_random_interval();
		rc = hb->llc_ops->sendnodemsg(hb, m, nodename);
	}

	if(del_flag) {
		g_free(memlist);
	}
	
	ha_msg_del(m);
	return(rc);
}

//
// send out a message to all the members of the cluster, asking for
// their connectivity information.
//
// NOTE: called by the cluster leader only.
//
static int
ccm_send_memlist_request(ll_cluster_t *hb, ccm_info_t *info)
{
	struct ha_msg *m;
	char majortrans[15]; /* 10 is the maximum number of digits in 
					UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/
	char *cookie;
	int  rc;

	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "ccm_send_memlist_request: Cannot allocate "
				"memory");
		return(HA_FAIL);
	}
	
	snprintf(majortrans, sizeof(majortrans), "%d", 
					CCM_GET_MAJORTRANS(info));
	snprintf(minortrans, sizeof(minortrans), "%d", 
					CCM_GET_MINORTRANS(info));
	cookie = CCM_GET_COOKIE(info);
	assert(cookie);

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_REQ_MEMLIST)) 
						== HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL)
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)) {
			ha_log(LOG_ERR, "ccm_send_memlist_request: Cannot "
				"create REQ_MEMLIST message");
		rc = HA_FAIL;
	} else {
		rc = hb->llc_ops->sendclustermsg(hb, m);
	}
	ha_msg_del(m);
	return(rc);
}




//
// Browse through the list of all the connectivity request messages
// from cluster leaders. Send out the connectivity information only
// to the node which we believe is the cluster leader. To everybody 
// else send out a null message.
//
static int
ccm_send_cl_reply(ll_cluster_t *hb, ccm_info_t *info)
{
	int ret=FALSE, bitmap_strlen;
	char *memlist, *cl, *cl_tmp;
	void *cltrack;
	int  trans;
	/*
        * Get the name of the cluster leader
	*/
	cl = update_get_cl_name(CCM_GET_UPDATETABLE(info), 
				CCM_GET_LLM(info));

	/* search through the update list and find if any Cluster
	 * leader has sent a memlist request. For each, check if
	 * that node is the one which we believe is the leader.
	 * if it is the leader, send it our membership list.
	 * if not send it an NULL membership reply.
	 */
	cltrack = update_initlink(CCM_GET_UPDATETABLE(info));
	while((cl_tmp = update_next_link(CCM_GET_UPDATETABLE(info), 
				CCM_GET_LLM(info), cltrack, &trans)) != NULL) {
		if(strncmp(cl, cl_tmp, 
			LLM_GET_NODEIDSIZE(CCM_GET_LLM(info))) == 0) {

			if(ccm_already_joined(info) && 
				CCM_GET_MAJORTRANS(info) != trans){
				fprintf(stderr, "evicted\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				fprintf(stderr, "********\n");
				ccm_reset(info);
				return FALSE;
			}
			ret = TRUE;
			bitmap_strlen = update_strcreate(
				CCM_GET_UPDATETABLE(info), 
				&memlist, CCM_GET_LLM(info));

			/* send Cluster Leader our memlist only if we are 
			 * operating in the same transition as that of 
			 * the leader, provided we have been a cluster member 
			 * in the past 
			 */
			while (ccm_send_memlist_res(hb, info, cl, memlist)
						!=HA_OK) {
				ha_log(LOG_ERR, "ccm_state_version_request: "
					"failure to send join");
					sleep(1);
			}
			update_strdelete(memlist);
		} else {
			/* I dont trust this Cluster Leader.
			Send NULL memlist message */
			while (ccm_send_memlist_res(hb, info, cl_tmp, NULL)
					!= HA_OK) {
				ha_log(LOG_ERR, 
				"ccm_state_version_request: failure "
						"to send join");
				sleep(1);
			}
		}
	}
	update_freelink(CCM_GET_UPDATETABLE(info), cltrack);
	update_free_memlist_request(CCM_GET_UPDATETABLE(info)); 
	return ret;
}
/////////////////////////////////////////////////////////////////////
//
// END OF FUNCTIONS THAT SEND OUT messages to nodes of the cluster
//
/////////////////////////////////////////////////////////////////////


//
// Fake up a leave message.
// This is generally done when heartbeat informs ccm of the crash of
// a cluster member.
//
static struct ha_msg *
ccm_create_leave_msg(ccm_info_t *info, int uuid)
{
	struct ha_msg *m;
	char majortrans[15]; /* 10 is the maximum number of digits in 
				UINT_MAX , adding a buffer of 5 */
	char minortrans[15]; /*		ditto 	*/
	llm_info_t *llm;
	char *nodename, *cookie;
	


	/* find the name of the node at index */
	llm = CCM_GET_LLM(info);
	nodename = llm_get_nodeid_from_uuid(llm, uuid);

    	if ((m=ha_msg_new(0)) == NULL) {
		ha_log(LOG_ERR, "ccm_send_memlist_request: "
				"Cannot allocate memory");
		return(HA_FAIL);
	}
	
	snprintf(majortrans, sizeof(majortrans), "%d", 
				CCM_GET_MAJORTRANS(info));
	snprintf(minortrans, sizeof(minortrans), "%d", 
				CCM_GET_MINORTRANS(info));
	cookie = CCM_GET_COOKIE(info);
	assert(cookie);

	if ((ha_msg_add(m, F_TYPE, ccm_type2string(CCM_TYPE_LEAVE)) 
							== HA_FAIL)
		||(ha_msg_add(m, F_ORIG, nodename) == HA_FAIL) 
		||(ha_msg_add(m, CCM_MAJORTRANS, majortrans) == HA_FAIL)
		||(ha_msg_add(m, CCM_COOKIE, cookie) == HA_FAIL)
		||(ha_msg_add(m, CCM_MINORTRANS, minortrans) == HA_FAIL)) {
		ha_log(LOG_ERR, "ccm_create_leave_msg: Cannot create REQ_LEAVE "
						    "message");
		return NULL;
	} 
	return(m);
}


//
// Watch out for new messages. As and when they arrive, return the
// message.
//
static struct ha_msg *
ccm_readmsg(ccm_info_t *info, ll_cluster_t *hb, int ttimeout)
{
	fd_set          fds;
	int             fd;
	int 		uuid;
	struct timeval	tv;

	assert(hb);
	fd = hb->llc_ops->inputfd(hb);

	/* check if there are any leave events to be delivered */
	while((uuid=leave_get_next()) != -1) {
		/* create a leave message and return it */
		return(ccm_create_leave_msg(info, uuid));
	}

	FD_ZERO(&fds);
	FD_SET(fd, &fds);
	tv.tv_sec = ttimeout;
	tv.tv_usec = 0;

	if (select(fd+1, &fds, NULL, NULL, &tv)) {
		return(hb->llc_ops->readmsg(hb, 0));
	}

	return NULL;
}





//
// Move the state of this ccm node, from joining state directly to
// the joined state.
//
// NOTE: this is generally called when a joining nodes determines
// that it is the only node in the cluster, and everybody else are
// dead.
//
static void
ccm_joining_to_joined(ll_cluster_t *hb, ccm_info_t *info)
{
	unsigned char *bitmap;
	char *cookie = NULL;

	/* create a bitmap with the membership information */
	(void) bitmap_create(&bitmap, MAXNODE);
	bitmap_mark(LLM_GET_MYUUID(CCM_GET_LLM(info)), bitmap, MAXNODE);

	/* 
	 * I am the only around! Lets discard any cookie that we
	 * got from others, and create a new cookie.
	 * This bug was noticed: when testing with partitioned
	 * clusters.
 	 */
	cookie = ccm_generate_random_cookie();

	/* fill my new memlist and update the new cookie if any */
	ccm_fill_memlist_from_bitmap(info, bitmap);
	bitmap_delete(bitmap);

	/* increment the major transition number and reset the
	 * minor transition number
	 */
	CCM_INCREMENT_MAJORTRANS(info); 
	CCM_RESET_MINORTRANS(info);

	/* if cookie has changed update it.
	 */
	if (cookie) {
		ha_log(LOG_INFO, "ccm_joining_to_joined: "
				"cookie changed ");
		CCM_SET_COOKIE(info, cookie); 
		ccm_free_random_cookie(cookie);
	}

	/* check if any joiner is waiting for a response from us. 
	 * If so respond 
	 */
	ccm_send_join_reply(hb, info);
	
	CCM_SET_CL(info, ccm_get_my_membership_index(info));
	update_reset(CCM_GET_UPDATETABLE(info));
	CCM_SET_STATE(info, CCM_STATE_JOINED);
	report_mbrs(info);
	if(!ccm_already_joined(info)) 
		CCM_SET_JOINED_TRANSITION(info, 1);
	return;
}

//
// Move the state of this ccm node, from init state directly to
// the joined state.
//
// NOTE: this is generally called when a node when it  determines
// that it is all alone in the cluster.
//
static void
ccm_init_to_joined(ccm_info_t *info)
{
	int numBytes;
	unsigned char *bitlist;
	char *cookie;

	numBytes = bitmap_create(&bitlist, MAXNODE);
	bitmap_mark(LLM_GET_MYUUID(CCM_GET_LLM(info)), bitlist,MAXNODE);
	ccm_fill_memlist_from_bitmap(info, bitlist);
	bitmap_delete(bitlist);
	CCM_SET_MAJORTRANS(info, 1);
	CCM_SET_MINORTRANS(info, 0);
	cookie = ccm_generate_random_cookie();
	CCM_SET_COOKIE(info, cookie);
	ccm_free_random_cookie(cookie);
	CCM_SET_CL(info, ccm_get_my_membership_index(info));
	CCM_SET_STATE(info, CCM_STATE_JOINED);
	CCM_SET_JOINED_TRANSITION(info, 1);
	report_mbrs(info);
	return;
}


#define MEMBERSHIP_INFO_FROM_FOLLOWERS_TIMEOUT  15
#define MEMBERSHIP_INFO_TO_FOLLOWERS_TIMEOUT  25
#define FINALLIST_TIMEOUT  MEMBERSHIP_INFO_TO_FOLLOWERS_TIMEOUT+5
#define UPDATE_TIMEOUT  1
#define UPDATE_LONG_TIMEOUT  45

//
// The state machine that processes message when it is
//	the CCM_STATE_VERSION_REQUEST state
//
static void
ccm_state_version_request(enum ccm_type ccm_msg_type,
			struct ha_msg *reply,
			ll_cluster_t *hb, 
			ccm_info_t *info)
{
	const char *orig, *proto, *cookie, *trans, *clsize;
	int trans_val, proto_val, clsize_val;

	/* who sent this message */
	if ((orig = ha_msg_value(reply, F_ORIG)) == NULL) {
		ha_log(LOG_ERR, "ccm_state_version_request: "
			"received message from unknown");
		return;
	}

	if(!llm_is_valid_node(CCM_GET_LLM(info), orig)) { 
		ha_log(LOG_ERR, "ccm_state_version_request: "
			"received message from unknown host %s", orig);
		return;
	}

	switch (ccm_msg_type)  {

	case CCM_TYPE_PROTOVERSION_RESP:

		/* get the protocol version */
		if ((proto = ha_msg_value(reply, CCM_PROTOCOL)) == NULL) {
			ha_log(LOG_ERR, "ccm_state_version_request: "
					"no protocol information");
			return;
		}

		proto_val = atoi(proto); /*TOBEDONE*/
		if (proto_val >= CCM_VER_LAST) {
			ha_log(LOG_ERR, "ccm_state_version_request: "
					"unknown protocol value");
			ccm_reset(info);
			return;
		}


		/* if this reply has come from a node which is a member
		 * of a larger cluster, we will try to join that cluster
		 * else we will wait for some time, by dropping this
		 * response.
		 */
		if(resp_can_i_drop()) {
			if ((clsize = ha_msg_value(reply, CCM_CLSIZE)) == NULL){
				ha_log(LOG_ERR, "ccm_state_version_request: "
						" no cookie information");
				return;
			}
			clsize_val = atoi(clsize);
			if(clsize_val < 
			   (llm_get_active_nodecount(CCM_GET_LLM(info))/2-1)) {
				/* drop the response. We will wait for 
			  	 * a response from a bigger group 
				 */
				resp_dropped();
				usleep(1000); /* sleep for a while */
				/* send a fresh version request message */
				version_reset(CCM_GET_VERSION(info));
				CCM_SET_STATE(info, CCM_STATE_NONE);
				/* free all the joiners that we accumulated */
				g_slist_free(CCM_GET_JOINERHEAD(info));
				CCM_SET_JOINERHEAD(info, NULL);
				break;
			} 
		}
		resp_reset();
	

		/* get the cookie string */
		if ((cookie = ha_msg_value(reply, CCM_COOKIE)) == NULL) {
			ha_log(LOG_ERR, "ccm_state_version_request: no cookie "
							"information");
			return;
		}

		/* get the major transition version */
		if ((trans = ha_msg_value(reply, CCM_MAJORTRANS)) == NULL) { 
			ha_log(LOG_ERR, "ccm_state_version_request: "
					"no protocol information");
			return;
		}

		trans_val = atoi(trans);

		/* send the join message to the cluster */
		CCM_SET_ACTIVEPROTO(info, proto_val);
		CCM_SET_MAJORTRANS(info, trans_val);
		CCM_SET_MINORTRANS(info, 0);
		CCM_SET_COOKIE(info, cookie);
		while (ccm_send_join(hb, info) != HA_OK) {
			ha_log(LOG_ERR, "ccm_state_version_request: failure to send join");
			sleep(1);
		}

		/* initialize the update table  and set our state to JOINING */
		update_reset(CCM_GET_UPDATETABLE(info));
		CCM_SET_STATE(info, CCM_STATE_JOINING);

		/* free all the joiners that we accumulated */
		g_slist_free(CCM_GET_JOINERHEAD(info));
		CCM_SET_JOINERHEAD(info, NULL);

		break;

        case CCM_TYPE_TIMEOUT:
		if(version_retry(CCM_GET_VERSION(info))){
			CCM_SET_STATE(info, CCM_STATE_NONE);
		} else {
			if(ccm_am_i_highest_joiner(info)) {
				if(global_debug)
					fprintf(stderr,"joined\n");
				ccm_init_to_joined(info);
			} else {
				if(global_debug)
					fprintf(stderr,"joined but not really\n");
				version_reset(CCM_GET_VERSION(info));
				CCM_SET_STATE(info, CCM_STATE_NONE);
			}
			/* free all the joiners that we accumulated */
			g_slist_free(CCM_GET_JOINERHEAD(info));
			CCM_SET_JOINERHEAD(info, NULL);
		}
		break;
				
	case CCM_TYPE_PROTOVERSION:
		/*
		 * cache this request. If we declare ourselves as
		 * a single member group, and if we find that
		 * somebody else also wanted to join the group.
		 * we will restart the join.
		 */
		ccm_add_new_joiner(info, orig);
		break;

        case CCM_TYPE_JOIN:
        case CCM_TYPE_REQ_MEMLIST:
        case CCM_TYPE_RES_MEMLIST:
        case CCM_TYPE_FINAL_MEMLIST:
        case CCM_TYPE_ABORT:
		/* note down there is some activity going 
		 * on and we are not yet alone in the cluster 
		 */
		version_some_activity(CCM_GET_VERSION(info));
		
        case CCM_TYPE_LEAVE:
        case CCM_TYPE_ERROR:
	default:
		/* nothing to do. Just forget the message */
		break;
	}

	return;
}

//
// The state machine that processes message when it is
//	CCM_STATE_JOINED state.
//
static void
ccm_state_joined(enum ccm_type ccm_msg_type, 
			struct ha_msg *reply, 
			ll_cluster_t *hb, 
			ccm_info_t *info)
{
	const char *orig,  *trans, *uptime;
	int  trans_majorval,trans_minorval=0, uptime_val;

	if ((orig = ha_msg_value(reply, F_ORIG)) == NULL) {
		ha_log(LOG_ERR, "ccm_state_joined: received message "
							"from unknown");
		return;
	}

	if(!llm_is_valid_node(CCM_GET_LLM(info), orig)) { 
		ha_log(LOG_ERR, "ccm_state_joined: received message "
				"from unknown host %s", orig);
		return;
	}

	if(ccm_msg_type != CCM_TYPE_PROTOVERSION) {

		if(strncmp(CCM_GET_COOKIE(info), 
			ha_msg_value(reply, CCM_COOKIE), COOKIESIZE) != 0){
			ha_log(LOG_ERR, "ccm_state_joined: received message "
					"with unknown cookie, just dropping");
			return;
		}



		/* get the major transition version */
		if ((trans = ha_msg_value(reply, CCM_MAJORTRANS)) == NULL) { 
			ha_log(LOG_ERR, "ccm_state_joined: no transition major "
				"information");
			return;
		}
		trans_majorval = atoi(trans);

	 	/*drop the message if it has lower major transition number */
		if (CCM_TRANS_EARLIER(trans_majorval,  
					CCM_GET_MAJORTRANS(info))) {
			ha_log(LOG_ERR, "ccm_state_joined: received "
				"CCM_TYPE_JOIN message with"
				"a earlier major transition number");
			return;
		}


		/* get the minor transition version */
		if ((trans = ha_msg_value(reply, CCM_MINORTRANS)) == NULL) { 
			ha_log(LOG_ERR, "ccm_state_joined: no transition minor "
					"information");
			return;
		}

		trans_minorval = atoi(trans);
	}

	switch (ccm_msg_type)  {
		case CCM_TYPE_PROTOVERSION_RESP:
			ha_log(LOG_ERR, "ccm_state_joined: dropping message "
				"of type %s.  Is this a Byzantime failure?", 
					ccm_type2string(ccm_msg_type));

			break;

		case CCM_TYPE_PROTOVERSION:
			/* If we were leader in the last successful itteration,
 			 * then we shall respond with the neccessary information
			 */
			if (ccm_am_i_leader(info)){
				while (ccm_send_joiner_reply(hb, info, orig)
						!= HA_OK) {
					ha_log(LOG_ERR, "ccm_state_joined: "
						"failure to send join reply");
						sleep(1);
				}
			}
			break;

		case CCM_TYPE_JOIN:
			/* get the update value */
			if ((uptime = ha_msg_value(reply, CCM_UPTIME)) == NULL){
				ha_log(LOG_ERR, "ccm_state_joined: no update "
						"information");
				return;
			}
			uptime_val = atoi(uptime);

			/* update the minor transition number if it is of 
			 * higher value and send a fresh JOIN message 
			 */
			assert (trans_minorval >= CCM_GET_MINORTRANS(info));
			update_reset(CCM_GET_UPDATETABLE(info));
			update_add(CCM_GET_UPDATETABLE(info), CCM_GET_LLM(info),
						orig, uptime_val);

			CCM_SET_MINORTRANS(info, trans_minorval);
			while (ccm_send_join(hb, info) != HA_OK) {
				ha_log(LOG_ERR, "ccm_state_joined: failure "
							"to send join");
				sleep(1);
			}

			CCM_SET_STATE(info, CCM_STATE_JOINING);
			break;	

		case CCM_TYPE_LEAVE: 
			/* If the leave is from a ccm member initiate a join 
			 * and go to JOINING state 
			 */
			if(ccm_get_membership_index(info, orig) == -1) break;
			while (ccm_send_join(hb, info) != HA_OK) {
				ha_log(LOG_ERR, "ccm_state_joined:"
				" failure to send join");
				sleep(1);
			}
			CCM_SET_STATE(info, CCM_STATE_JOINING);
			break;
			
		case CCM_TYPE_TIMEOUT:
			break;

		case CCM_TYPE_REQ_MEMLIST:
		case CCM_TYPE_RES_MEMLIST:
		case CCM_TYPE_FINAL_MEMLIST:
		case CCM_TYPE_ABORT:
		case CCM_TYPE_ERROR:
			ha_log(LOG_ERR, "ccm_state_joined: dropping message "
				"of type %s. Is this a Byzantime failure?", 
				ccm_type2string(ccm_msg_type));
			/* nothing to do. Just forget the message */
			break;
		default:
	}
}





//
// The state machine that processes message when it is
//	in the CCM_STATE_SENT_MEMLISTREQ state
//
static void
ccm_state_sent_memlistreq(enum ccm_type ccm_msg_type, 
			struct ha_msg *reply, 
			ll_cluster_t *hb, 
			ccm_info_t *info)
{
	const char *orig,  *trans, *memlist;
	int   trans_majorval=0, trans_minorval=0;

	if ((orig = ha_msg_value(reply, F_ORIG)) == NULL) {
		ha_log(LOG_ERR, "ccm_state_sent_memlistreq: received message "
						"from unknown");
		return;
	}

	if(!llm_is_valid_node(CCM_GET_LLM(info), orig)) { 
		ha_log(LOG_ERR, "ccm_state_sent_memlistreq: received message "
				"from unknown host %s", orig);
		return;
	}

	if(ccm_msg_type ==  CCM_TYPE_PROTOVERSION) goto switchstatement;

	if(strncmp(CCM_GET_COOKIE(info), ha_msg_value(reply, CCM_COOKIE), 
				COOKIESIZE) != 0){
		ha_log(LOG_ERR, "ccm_state_memlist_res: received message "
				"with unknown cookie, just dropping");
		return;
	}

	/* get the major transition version */
	if ((trans = ha_msg_value(reply, CCM_MAJORTRANS)) == NULL) { 
		ha_log(LOG_ERR, "ccm_state_sent_memlistreq:no transition major "
				"information");
		return;
	}

	trans_majorval = atoi(trans);
	 /*	drop the message if it has lower major transition number */
	if (CCM_TRANS_EARLIER(trans_majorval,  CCM_GET_MAJORTRANS(info))) {
		ha_log(LOG_ERR, "ccm_state_sent_memlistreq: received "
					"CCM_TYPE_JOIN message with"
					"a earlier major transition number");
		return;
	}


	/* get the minor transition version */
	if ((trans = ha_msg_value(reply, CCM_MINORTRANS)) == NULL) { 
		ha_log(LOG_ERR, "ccm_state_sent_memlistreq:no transition minor "
				"information");
		return;
	}

	trans_minorval = atoi(trans);

switchstatement:
	switch (ccm_msg_type)  {
		case CCM_TYPE_PROTOVERSION_RESP:

			ha_log(LOG_ERR, "ccm_state_sent_memlistreq: "
				"dropping message of type %s. "
				" Is this a Byzantime failure?",
				ccm_type2string(ccm_msg_type));

			break;


		case CCM_TYPE_PROTOVERSION:
			/*
			 * cache this request. We will respond to it, 
			 * if we become the leader.
			 */
			ccm_add_new_joiner(info, orig);
			
			break;

		case CCM_TYPE_JOIN:
			/* The join request has come too late.
			 * I am already the leader, and my
			 * leadership cannot be relinquished
			 * because that can confuse everybody.
			 * This join request shall be considered
			 * only if the requestor cannot compete be
			 * a leader.
			 */
			assert(trans_majorval == CCM_GET_MAJORTRANS(info));
			assert(trans_minorval == CCM_GET_MINORTRANS(info));
			fprintf(stderr, "considering a late join message "
					  "from orig=%s\n", orig);
			ccm_add_membership(info, orig);
			break;

		case CCM_TYPE_TIMEOUT:
			if (ccm_membership_timeout(info,
				MEMBERSHIP_INFO_FROM_FOLLOWERS_TIMEOUT)) {
				/* we waited long for membership response 
				 * from all nodes, stop waiting and send
				 * final membership list
				 */
				if(ccm_membership_timeout(info,
					MEMBERSHIP_INFO_TO_FOLLOWERS_TIMEOUT)) {
					/* its too long since I declared myself
					 * as the leader. Nobody else will
					 * be waiting on us. So just send
					 * leave message and reset.
				 	 */
					while (ccm_send_leave(hb, info) != HA_OK) {
						ha_log(LOG_ERR, "ccm_state_memlistreq: "
							"failure to send leave");
						sleep(1);
					}
					ccm_reset(info);
				} else {
					ccm_compute_and_send_final_memlist(hb, info);
				}
			}
			break;

		case CCM_TYPE_REQ_MEMLIST:

			/* if this is my own message just forget it */
			if(strncmp(orig, ccm_get_my_hostname(info),
				LLM_GET_NODEIDSIZE(CCM_GET_LLM(info))) == 0) 
				break;


			/* whoever is requesting memlist from me thinks it is 
			 * the leader. Hmm....., we will send it a NULL memlist.
			 * In partitioned network case both of us can be 
			 * leaders. Right?
			 */


			while (ccm_send_memlist_res(hb, info, orig, NULL) != 
						HA_OK) {
				ha_log(LOG_ERR, "ccm_state_sent_memlistreq: "
					"failure to send join");
				sleep(1);
			}
			break;

		case CCM_TYPE_RES_MEMLIST:
			/* mark that this node has sent us a memlist reply.
			 * Calculate the membership list with this new message 
			 */
			if(trans_minorval != CCM_GET_MINORTRANS(info)) break;
			if(trans_majorval != CCM_GET_MAJORTRANS(info)) {
				fprintf(stderr, "dropping CCM_TYPE_RES_MEMLIST "
				   "from orig=%s mymajor=%d msg_major=%d\n", 
				   orig, trans_majorval, 
					CCM_GET_MAJORTRANS(info));
				assert(0);
				break;
			}
			if ((memlist = ha_msg_value(reply, CCM_MEMLIST)) 
						== NULL) { 
				ha_log(LOG_ERR, "ccm_state_sent_memlistreq: "
						"no memlist ");
				break;
			}
			ccm_note_membership(info, orig, memlist);

			if (ccm_rcvd_all_memlist(info)) {
				if(ccm_membership_timeout(info,
					MEMBERSHIP_INFO_TO_FOLLOWERS_TIMEOUT)) {
					/* its too long since I declared myself
					 * as the leader. Nobody else will
					 * be waiting on us. So just send
					 * leave message and reset.
				 	 */
					while (ccm_send_leave(hb, info) != HA_OK) {
						ha_log(LOG_ERR, "ccm_state_memlistreq: "
							"failure to send leave");
						sleep(1);
					}
					ccm_reset(info);
				} else {
					ccm_compute_and_send_final_memlist(hb, info);
				}
			}
			break;

		case CCM_TYPE_LEAVE: 
			/* since we are waiting for a memlist from all the 
			 * members who have sent me a join message, we 
			 * should be waiting for their message or their 
			 * leave message atleast.
			 */

			/* if this node had not participated in the update 
			 * exchange than just neglect it 
			 */
			if(!update_is_member(CCM_GET_UPDATETABLE(info), 
					CCM_GET_LLM(info), orig)) break;
			
			/* if this node had sent a memlist before dying,
			 * reset its memlist information */
			if(ccm_membership_already_noted(info, orig)) {
				ccm_modify_membership(info, orig, "");
			} else {
				ccm_note_membership(info, orig, "");
			}

			if (ccm_rcvd_all_memlist(info)) {
				if(ccm_membership_timeout(info,
					MEMBERSHIP_INFO_TO_FOLLOWERS_TIMEOUT)) {
					/* its too long since I declared myself
					 * as the leader. Nobody else will
					 * be waiting on us. So just send
					 * leave message and reset.
				 	 */
					while (ccm_send_leave(hb, info) != HA_OK) {
						ha_log(LOG_ERR, "ccm_state_memlistreq: "
							"failure to send leave");
						sleep(1);
					}
					ccm_reset(info);
				} else {
					ccm_compute_and_send_final_memlist(hb, info);
				}
			}
			break;
				
		case CCM_TYPE_FINAL_MEMLIST:
		case CCM_TYPE_ABORT:
		case CCM_TYPE_ERROR:
		default:
			ha_log(LOG_ERR, "ccm_state_sent_memlistreq: "
					"dropping message of type %s. Is this "
					"a Byzantime failure?", 
					ccm_type2string(ccm_msg_type));
			/* nothing to do. Just forget the message */
			break;
	}
}

//
// the state machine that processes messages when it is in the
// CCM_STATE_MEMLIST_RES state.
//
static void
ccm_state_memlist_res(enum ccm_type ccm_msg_type, 
		struct ha_msg *reply, 
		ll_cluster_t *hb, 
		ccm_info_t *info)
{
	const char *orig,  *trans, *uptime, *memlist, *cookie, *cl;
	int   trans_majorval=0, trans_minorval=0, uptime_val;
	int  curr_major, curr_minor, indx;



	if ((orig = ha_msg_value(reply, F_ORIG)) == NULL) {
		ha_log(LOG_ERR, "ccm_state_memlist_res: received message "
							"from unknown");
		return;
	}

	if(!llm_is_valid_node(CCM_GET_LLM(info), orig)) { 
		ha_log(LOG_ERR, "ccm_state_memlist_res: received message "
				"from unknown host %s", orig);
		return;
	}

	if(ccm_msg_type ==  CCM_TYPE_PROTOVERSION) goto switchstatement;

	if(strncmp(CCM_GET_COOKIE(info), ha_msg_value(reply, CCM_COOKIE), 
				COOKIESIZE) != 0){
		ha_log(LOG_ERR, "ccm_state_memlist_res: received message "
				"with unknown cookie, just dropping");
		return;
	}

	/* get the major transition version */
	if ((trans = ha_msg_value(reply, CCM_MAJORTRANS)) == NULL) { 
		ha_log(LOG_ERR, "ccm_state_memlist_res: no transition major "
				"information");
		return;
	}

	trans_majorval = atoi(trans);
	 /*	drop the message if it has lower major transition number */
	if (CCM_TRANS_EARLIER(trans_majorval,  CCM_GET_MAJORTRANS(info))) {
		ha_log(LOG_ERR, "ccm_state_memlist_res: received "
					"CCM_TYPE_JOIN message with"
					"a earlier major transition number");
		return;
	}


	/* get the minor transition version */
	if ((trans = ha_msg_value(reply, CCM_MINORTRANS)) == NULL) { 
		ha_log(LOG_ERR, "ccm_state_memlist_res: no transition minor "
				"information");
		return;
	}

	trans_minorval = atoi(trans);


switchstatement:

	switch (ccm_msg_type)  {
		case CCM_TYPE_PROTOVERSION_RESP:
			ha_log(LOG_ERR, "ccm_state_memlist_res:dropping message"
					" of type %s. Is this a Byzantime "
					" failure?", 
					ccm_type2string(ccm_msg_type));
			break;

		case CCM_TYPE_PROTOVERSION:
			/*
			 * cache this request. We will respond to it, if we 
			 * become the leader.
			 */
			ccm_add_new_joiner(info, orig);
			
			break;

		case CCM_TYPE_JOIN:

			/*
			 * This could have happened because the leader died 
			 * and somebody noticed this and sent us this request. 
			 * In such a case the minor transition number should 
			 * have incremented. Or
			 * This could have happened because the leader's 
			 * FINAL_MEMLIST	
			 * has not reach us, whereas it has reached somebody 
			 * else, and since that somebody saw a change in 
			 * membership, initiated another join protocol. 
			 * In such a case the major transition
			 * number should have incremented.
			 */
			/* 
			 * if major number is incremented, send an abort message
			 * to the sender. The sender must resend the message.
			 */
			if (trans_majorval > CCM_GET_MAJORTRANS(info)) {
				while (ccm_send_abort(hb, info, orig, 
					trans_majorval, trans_minorval) 
							!= HA_OK) {
					ha_log(LOG_ERR, "ccm_state_memlist_res:"
						" failure to send join");
					sleep(1);
				}
				break;
			}

			/* if minor transition number is incremented, 
			 * reset uptable table and start a join protocol
			 */
			if (trans_minorval > CCM_GET_MINORTRANS(info)) {
				/* get the update value */
				if ((uptime = ha_msg_value(reply, CCM_UPTIME)) 
							== NULL){
					ha_log(LOG_ERR, 
						"ccm_state_memlist_res: no "
						"update information");
					return;
				}
				uptime_val = atoi(uptime);

				update_reset(CCM_GET_UPDATETABLE(info));
				update_add(CCM_GET_UPDATETABLE(info), 
					CCM_GET_LLM(info), orig, uptime_val);

				CCM_SET_MINORTRANS(info, trans_minorval);
				while (ccm_send_join(hb, info) != HA_OK) {
					ha_log(LOG_ERR, "ccm_state_memlist_res:"
						" failure to send join");
					sleep(1);
				}
				CCM_SET_STATE(info, CCM_STATE_JOINING);
			}

			break;
			

		case CCM_TYPE_REQ_MEMLIST:
			/* there are two reasons that can bring us here 
			 * 1. Because some other node still thinks he is 
			 * the master,(though we dont think so). Send 
			 * a NULL membership list to him immidiately.
			 * 2. Because of byzantine failures, though we have 
			 * not recieved the the membership list in the last 
			 * round. We have waited to such an exent that some 
			 * node already thinks he is the master of the
			 * the new group transition. Well, there is something 
			 * seriously wrong with us. We will send a leave 
			 * message to everybody and say good bye. And we 
			 * will start all fresh!
			 */
			if (trans_minorval == CCM_GET_MINORTRANS(info)) {
				while (ccm_send_memlist_res(hb, info, orig, 
							NULL) != HA_OK) {
					ha_log(LOG_ERR, "ccm_state_memlist_res:"
					 " failure to send join");
					sleep(1);
				}
				break;
			}

			/* all other cases are cases of byzantine failure 
			 * We leave the cluster
			 */
			while (ccm_send_leave(hb, info) != HA_OK) {
				ha_log(LOG_ERR, "ccm_state_memlist_res: "
					"failure to send join");
				sleep(1);
			}

			ccm_reset(info); 
			break;

        	case CCM_TYPE_TIMEOUT:
			/* If we have waited too long for the leader to respond
			 * just assume that the leader is dead and start over
			 * a new round of the protocol
			 */
			if(!finallist_timeout(FINALLIST_TIMEOUT)) {
				break;
			}
			update_reset(CCM_GET_UPDATETABLE(info));
			CCM_INCREMENT_MINORTRANS(info);
			while (ccm_send_join(hb, info) != HA_OK) {
				ha_log(LOG_ERR, "ccm_state_memlist_res:"
				" failure to send join");
				sleep(1);
			}
			finallist_reset();
			CCM_SET_STATE(info, CCM_STATE_JOINING);
			break;

		case CCM_TYPE_LEAVE: 
			/* 
			 * If this message is because of loss of connectivity 
			 * with the node which we think is the master, then 
			 * restart the join. Loss of anyother node should be 
			 * confirmed by the finalmemlist of the master.
		 	 */
			cl = update_get_cl_name(CCM_GET_UPDATETABLE(info), 
					CCM_GET_LLM(info));
			if(strncmp(cl, orig, 
				LLM_GET_NODEIDSIZE(CCM_GET_LLM(info))) == 0) {
				/* increment the current minor transition value 
				 * and resend the join message 
				 */
				update_reset(CCM_GET_UPDATETABLE(info));
				CCM_INCREMENT_MINORTRANS(info);
				while (ccm_send_join(hb, info) != HA_OK) {
					ha_log(LOG_ERR, "ccm_state_memlist_res:"
					" failure to send join");
					sleep(1);
				}
				finallist_reset();
				CCM_SET_STATE(info, CCM_STATE_JOINING);
			}

			break;
		
		case CCM_TYPE_FINAL_MEMLIST:
			/* WOW we received the membership list from the master.
			 * Check if I am part of the membership list. If not, 
			 * voluntarily leave the cluster and start all over 
			 * again 
			 */
			cl = update_get_cl_name(CCM_GET_UPDATETABLE(info), 
						CCM_GET_LLM(info));

			if(strncmp(cl, orig, 
				LLM_GET_NODEIDSIZE(CCM_GET_LLM(info))) != 0) {
				/* received memlist from a node we do not 
				 * think is the leader. We just reject the 
				 * message and wait for a message from the 
				 * our percieved master
				 */
				ha_log(LOG_ERR, "ccm_state_memlist_res: "
					"received final memlist from "
					"non-master,neglecting");
									
				break;
			}
	
			/* 
			 * confirm that the major transition and minor 
			 * transition version match
			 */
			curr_major = CCM_GET_MAJORTRANS(info);
			curr_minor = CCM_GET_MINORTRANS(info);

			if(curr_major != trans_majorval || 
				curr_minor !=  trans_minorval){
				ha_log(LOG_ERR, "ccm_state_memlist_res: "
					"received final memlist from master, "
					"but transition versions do not match: "
					"rejecting the message");
				break;
			}
			
			if ((memlist = ha_msg_value(reply, CCM_MEMLIST)) 
						== NULL) { 
				ha_log(LOG_ERR, "ccm_state_sent_memlistreq: "
						"no membership list ");
				return;
			}

			if (!ccm_am_i_member(info, memlist)) {
				ccm_reset(info); 
				break;
			}

			ccm_fill_memlist_from_str(info, memlist);
			/* increment the major transition number and reset the
			 * minor transition number
			 */
			CCM_INCREMENT_MAJORTRANS(info); 
			CCM_RESET_MINORTRANS(info);

			/* check if leader has changed the COOKIE, this can
			 * happen if the leader sees a partitioned group
			 */
			if ((cookie = ha_msg_value(reply, CCM_NEWCOOKIE)) 
						!= NULL) { 
				ha_log(LOG_INFO, "ccm_state_sent_memlistreq: "
					"leader  changed  cookie ");
				CCM_SET_COOKIE(info, cookie); 
			}

			indx = ccm_get_membership_index(info, cl); 
			assert(indx != -1);
			CCM_SET_CL(info, indx); 
			report_mbrs(info); /* call before update_reset */
			update_reset(CCM_GET_UPDATETABLE(info));
			finallist_reset();
			CCM_SET_STATE(info, CCM_STATE_JOINED);
			if(!ccm_already_joined(info)) 
				CCM_SET_JOINED_TRANSITION(info, 
					CCM_GET_MAJORTRANS(info));
			break;


		case CCM_TYPE_ABORT:
		case CCM_TYPE_RES_MEMLIST:
		case CCM_TYPE_ERROR:
		default:
			ha_log(LOG_ERR, "ccm_state_sendmemlistreq: "
					"dropping message of type %s. "
					"Is this a Byzantime failure?", 
					ccm_type2string(ccm_msg_type));
			/* nothing to do. Just forget the message */
			break;
	}
}



//
// the state machine that processes messages when it is in the
// CCM_STATE_JOINING state.
//
static void
ccm_state_joining(enum ccm_type ccm_msg_type, 
		struct ha_msg *reply, 
		ll_cluster_t *hb, 
		ccm_info_t *info)
{
	const char *orig,  *trans, *uptime;
	int   trans_majorval=0, trans_minorval=0, uptime_val;

	if ((orig = ha_msg_value(reply, F_ORIG)) == NULL) {
		ha_log(LOG_ERR, "ccm_state_joining: received message "
							"from unknown");
		return;
	}

	if(!llm_is_valid_node(CCM_GET_LLM(info), orig)) { 
		ha_log(LOG_ERR, "ccm_state_joining: received message "
				"from unknown host %s", orig);
		return;
	}

	if(ccm_msg_type ==  CCM_TYPE_PROTOVERSION) goto switchstatement;

	if(strncmp(CCM_GET_COOKIE(info), ha_msg_value(reply, CCM_COOKIE), 
			COOKIESIZE) != 0){
		ha_log(LOG_ERR, "ccm_state_joining: received message "
			"with unknown cookie, just dropping");
		return;
	}

	


	/* get the major transition version */
	if ((trans = ha_msg_value(reply, CCM_MAJORTRANS)) == NULL) { 
		ha_log(LOG_ERR, "ccm_state_joining: no transition major "
				"information");
		return;
	}

	trans_majorval = atoi(trans);
	 /*	drop the message if it has lower major transition number */
	if (CCM_TRANS_EARLIER(trans_majorval,  CCM_GET_MAJORTRANS(info))) {
		ha_log(LOG_ERR, "ccm_state_joining: received "
				"CCM_TYPE_JOIN message with"
				"a earlier major transition number");
		return;
	}


	/* get the minor transition version */
	if ((trans = ha_msg_value(reply, CCM_MINORTRANS)) == NULL) { 
		ha_log(LOG_ERR, "ccm_state_joining: no transition minor "
				"information");
		return;
	}

	trans_minorval = atoi(trans);
	if (trans_minorval < CCM_GET_MINORTRANS(info)) {
		return;
	}


switchstatement:
	switch (ccm_msg_type)  {

		case CCM_TYPE_PROTOVERSION_RESP:

			/* If we were joined in an earlier iteration, then this
			 * message should not have arrived. A bug in the logic!
			 */
			if(ccm_already_joined(info)) {
				ha_log(LOG_ERR, "ccm_state_joining: BUG:"
					" received CCM_TYPE_PROTOVERSION_RESP "
					"message when we have not asked for "
					"it ");
				break;
			}

			ha_log(LOG_ERR, "ccm_state_joining: dropping message "
					" of type %s. Is this a Byzantime "
					"failure?", 
					ccm_type2string(ccm_msg_type));
			break;
				

		case CCM_TYPE_PROTOVERSION:
			/*
			 * cache this request. We will respond to it, 
			 * if we become the leader.
			 */
			ccm_add_new_joiner(info, orig);
			
			break;

        	case CCM_TYPE_JOIN:
			/* get the update value */
			if((uptime = ha_msg_value(reply, CCM_UPTIME)) == NULL){ 
				ha_log(LOG_ERR, "ccm_state_joining: no update "
						"information");
				return;
			}
			uptime_val = atoi(uptime);

			/* 
			 * note down all the information contained in the 
			 * message There is a possibility that I am the leader,
			 * if all the nodes died, and I am the only surviving 
			 * node! If this message has originated from me, 
			 * note down the current time. This information is 
			 * needed, to later recognize that I am the only 
			 * surviving node.
			 */
			/* update the minor transition number if it is of 
			 * higher value 
			 * and send a fresh JOIN message 
			 */
			if (trans_minorval > CCM_GET_MINORTRANS(info)) {
				update_reset(CCM_GET_UPDATETABLE(info));
				update_add( CCM_GET_UPDATETABLE(info),
					CCM_GET_LLM(info), orig, uptime_val);

				CCM_SET_MINORTRANS(info, trans_minorval);
				while (ccm_send_join(hb, info) != HA_OK) {
					ha_log(LOG_ERR, 
					 "ccm_state_version_request: failure "
						"to send join");
					sleep(1);
				}
			} else {
				/* update the update table  */
				update_add( CCM_GET_UPDATETABLE(info),
					CCM_GET_LLM(info), orig, uptime_val);

				/* if all nodes have responded, its time 
				 * to elect the leader 
				 */
				if (UPDATE_GET_NODECOUNT(
					CCM_GET_UPDATETABLE(info)) ==
					CCM_GET_LLM_NODECOUNT(info)) {

					/* check if I am the leader */
					if (update_am_i_leader(
						CCM_GET_UPDATETABLE(info),
						CCM_GET_LLM(info))) {
						/* send out the 
						 * membershiplist request */
						while(ccm_send_memlist_request(
							hb, info)!=HA_OK) {
							ha_log(LOG_ERR, 
							"ccm_state_joining: "
							"failure to send "
							"memlist request");
							sleep(1);
						}
						ccm_membership_init(info);
						ccm_note_my_membership(info);
						CCM_SET_STATE(info, 
						  CCM_STATE_SENT_MEMLISTREQ);
					} else {
						/* check if we have already 
						 * received memlist request
						 * from any node(which 
						 * believes itself to be the 
						 * leader)
						 * If so,we have to reply to 
						 * them with our membership
						 * list. But there is a catch. 
						 * If we do not think the
						 * requestor to be the leader, 
						 * then we send it an null
						 * membership message!
						 */
						if (ccm_send_cl_reply(hb,info) 
								== TRUE) {
							finallist_init();
							CCM_SET_STATE(info, 
							 CCM_STATE_MEMLIST_RES);
						}
					}
					break; /* done all processing */
				} 
			}
				   
			break;	

		case CCM_TYPE_REQ_MEMLIST:

			/* well we have not yet timedout! And a memlist
			 * request has arrived from the cluster leader.  Hmm...
			 * We should wait till timeout, to respond.
			 *
			 * NOTE:  there is a chance
			 * that more than one cluster leader might request
			 * the membership list. Due to cluster partitioning :( )
			 */
			update_add_memlist_request(CCM_GET_UPDATETABLE(info),
					CCM_GET_LLM(info), orig, trans_majorval);
			/*
			 * FALL THROUGH
			 */
		case CCM_TYPE_TIMEOUT:
			/*
			 * If timeout expired, elect the leader.
			 * If I am the leader, send out the membershiplist request
			 */
			if (!update_timeout_expired(CCM_GET_UPDATETABLE(info), 
							UPDATE_TIMEOUT))
						break;

			if (update_am_i_leader(CCM_GET_UPDATETABLE(info),
						CCM_GET_LLM(info))) {

				/* if I am the only one around go directly
				 * to joined state.
				 */
				if (UPDATE_GET_NODECOUNT(
					CCM_GET_UPDATETABLE(info)) == 1) {
					ccm_joining_to_joined(hb, info);
					break;
				}

				/* send out the membershiplist request */
				while (ccm_send_memlist_request(hb, info) 
							!= HA_OK) {
					ha_log(LOG_ERR, "ccm_state_joining: "
						"failure to send memlist "
						"request");
					sleep(1);
				}
				ccm_membership_init(info);
				ccm_note_my_membership(info);
				CCM_SET_STATE(info, CCM_STATE_SENT_MEMLISTREQ);
			} else {
				/* check if we have already received memlist 
				 * request from any node(which believes itself 
				 * to be the leader)
				 * If so,we have to reply to them with our 
				 * membership list. But there is a catch. 
				 * If we do not think the
				 * requestor to be the leader, then we send 
				 * it an abort message!
				 */
				if (ccm_send_cl_reply(hb, info) == TRUE) {
					/* free the update data*/
					finallist_init();
					CCM_SET_STATE(info, CCM_STATE_MEMLIST_RES);
				} else if(update_timeout_expired(
						CCM_GET_UPDATETABLE(info),
						UPDATE_LONG_TIMEOUT)) {
					while (ccm_send_leave(hb, info) 
							!= HA_OK) {
					   	ha_log(LOG_ERR, 
							"ccm_state_joining: "
					 		"failure to send leave");
						sleep(1);
					}
					ccm_reset(info);
					CCM_SET_STATE(info, CCM_STATE_NONE);
				}
			}
			break;


		case CCM_TYPE_ABORT:

			/*
			 * This is a case where my JOIN request is not honoured
			 * by the recieving host(probably because it is waiting
			 * on some message, before which it cannot initiate 
			 * the join).
			 * We will resend the join message, incrementing the
			 * minor version number, provided this abort is 
			 * requested
			 * for this minor version.
			 */
			if(trans_majorval != CCM_GET_MAJORTRANS(info) ||
				trans_minorval != CCM_GET_MINORTRANS(info)) {
				/* nothing to worry  just forget this message */
					break;
			}
					
			/* increment the current minor transition value 
			 * and resend the
				join message */
			CCM_INCREMENT_MINORTRANS(info);
			while (ccm_send_join(hb, info) != HA_OK) {
				ha_log(LOG_ERR, "ccm_state_joining: failure "
					"to send join");
				sleep(1);
			}

			break;


		case CCM_TYPE_LEAVE: 

			/* 
			 * Has that node already sent a valid update message 
			 * before death. If so, remove him from the update 
			 * table.
			 */
			update_remove(CCM_GET_UPDATETABLE(info),
					CCM_GET_LLM(info), 
					orig);
			/* if we have any cached version-request from this node 
			 * we will get rid of that too
			 */
			ccm_remove_new_joiner(info, orig);
			break;


		case CCM_TYPE_RES_MEMLIST:
		case CCM_TYPE_FINAL_MEMLIST:
		case CCM_TYPE_ERROR:
		default:
			ha_log(LOG_ERR, "ccm_state_joining: dropping message "
				"of type %s. Is this a Byzantime failure?", 
					ccm_type2string(ccm_msg_type));
			/* nothing to do. Just forget the message */
			break;
	}
	return;
}


// 
// The most important function which tracks the state machine.
// 
static void
ccm_control_init(ccm_info_t *info)
{
	ccm_init(info);

	/* if this is the only active node in the cluster, go to the 
			JOINED state */
	if (llm_get_active_nodecount(CCM_GET_LLM(info)) == 1) {
		ccm_init_to_joined(info);
	} else {
		CCM_SET_STATE(info, CCM_STATE_NONE);
	}

	return;
}



//
// The callback function which is called when the status of a link
// changes.
//
static void
LinkStatus(const char * node, const char * lnk, const char * status ,
		void * private)
{
        fprintf(stderr, "Link Status update: Link %s/%s now has status %s\n",
				node, lnk, status);
}


//
// The callback function which is called when the status of a node
// changes.
//
static void
nodelist_update(const char *id, const char *status, void *private)
{
	ccm_info_t *info = (ccm_info_t *)private;
	llm_info_t *llm;
	int indx, uuid;
	
	/* update the low level membership of the node
	 * if the status moves from active to dead and if the member
	 * is already part of the ccm, then we have to mimic a
	 * leave message for us 
	 */
	if(global_debug)
		fprintf(stderr, "nodelist update: Link %s now has status %s\n",
				id,  status);
	llm = CCM_GET_LLM(info);
	if(llm_status_update(llm, id, status)) {
		indx = ccm_get_membership_index(info,id);
		if(indx != -1) {
			uuid = llm_get_uuid(llm, id);
			leave_cache(uuid);
		}
	}
	return;
}

static struct ha_msg*
ccm_handle_hbapiclstat(ccm_info_t *info,  
		const char *orig, 
		const char *status)
{
	int 		uuid;
	enum ccm_state 	state = CCM_GET_STATE(info);
	
	if(state == CCM_STATE_NONE ||
		state == CCM_STATE_VERSION_REQUEST) {
		return NULL;
	}

	assert(status);
	if(strncmp(status, "join", 5) == 0) {
		fprintf(stderr, "ignoring join "
		"message from orig=%s\n", orig);
		return NULL;
	}

	if(!orig) return NULL;
	uuid = llm_get_uuid(CCM_GET_LLM(info), orig);
	if(uuid == -1) return NULL;
	return(ccm_create_leave_msg(info, uuid));
}


static struct ha_msg*
ccm_handle_shutdone(ccm_info_t *info,
		const char *orig, 
		const char *status)
{
	int 		uuid;
	enum ccm_state 	state = CCM_GET_STATE(info);
	
	if(state == CCM_STATE_NONE ||
		state == CCM_STATE_VERSION_REQUEST) {
		return timeout_msg_mod(info);
	}
	if(!orig) return timeout_msg_mod(info);
	if(strncmp(ccm_get_my_hostname(info),orig, 
		LLM_GET_NODEIDSIZE(CCM_GET_LLM(info))) == 0) {
		ccm_reset(info);
		return NULL;
	}
	uuid = llm_get_uuid(CCM_GET_LLM(info), orig);
	if(uuid == -1) return timeout_msg_mod(info);
	return(ccm_create_leave_msg(info, uuid));
}

// 
// The most important function which tracks the state machine.
// 
static int
ccm_control_process(ccm_info_t *info, ll_cluster_t * hb)
{
	struct ha_msg *reply;
	const char *type;
	enum ccm_type ccm_msg_type;
	const char *orig=NULL;
	const char *status=NULL;

	/* read the next available message */

	reply = ccm_readmsg(info, hb, 0); /* wait no sec for timeout */

	if (reply) {
		type = ha_msg_value(reply, F_TYPE);
		orig = ha_msg_value(reply, F_ORIG);
		status = ha_msg_value(reply, F_STATUS);
		if(strncmp(type, "hbapi-clstat", TYPESTRSIZE) == 0){
			ha_msg_del(reply);
		       	if((reply = ccm_handle_hbapiclstat(info, orig, status)) 
					== NULL) {
				return 0;
			}
		} else if((strncmp(type, "shutdone", TYPESTRSIZE)) == 0) {
			ha_msg_del(reply);
		       	if((reply = ccm_handle_shutdone(info, orig, status)) 
					== NULL) {
				return 1;
			}
		} 
	} else {
		reply = timeout_msg_mod(info);
	}

	type = ha_msg_value(reply, F_TYPE);
	ccm_msg_type = ccm_string2type(type);
	if (ccm_msg_type == CCM_TYPE_ERROR) {
		fprintf(stderr, 
			"received message %s orig=%s\n",
			type, 
			ha_msg_value(reply, F_ORIG));
		ha_msg_del(reply);
		return 0;
	}

	if(global_debug)
		fprintf(stderr, "received message %s orig=%s\n", 
			type, ha_msg_value(reply, F_ORIG));

	switch(CCM_GET_STATE(info)) {

	case CCM_STATE_NONE:
		/* request for protocol version and transition 
		 * number for compatibility 
		 */
		while(ccm_send_protoversion(hb, info) != HA_OK) {
			ha_log(LOG_ERR, "ccm_control_process:failure to send "
					"protoversion request");
			sleep(1);
		}
		CCM_SET_STATE(info, CCM_STATE_VERSION_REQUEST);
		/* 
		 * FALL THROUGH 
		 */

	case CCM_STATE_VERSION_REQUEST:

		ccm_state_version_request(ccm_msg_type, reply, hb, 
							info);
		break;


	case CCM_STATE_JOINING:

		ccm_state_joining(ccm_msg_type, reply, hb, 
						info);
		break;


	case CCM_STATE_SENT_MEMLISTREQ:

		ccm_state_sent_memlistreq(ccm_msg_type, reply, hb, 
						info);
		break;

	case CCM_STATE_MEMLIST_RES:

		ccm_state_memlist_res(ccm_msg_type, reply, hb, 
						info);
		break;

	case CCM_STATE_JOINED:

		ccm_state_joined(ccm_msg_type, reply, hb, 
						info);
		break;

	default:
		fprintf(stderr, "INTERNAL LOGIC ERROR\n");
		return(1);
	}

	if(ccm_msg_type != CCM_TYPE_TIMEOUT) ha_free(reply);

	return 0;
}



/*
 * datastructure passed to the event loop.
 * This acts a handle, and should not be interpreted
 * by the event loop.
 */
typedef struct  ccm_s {
	ll_cluster_t    *hbfd;
	void    	*info;
} ccm_t;

//  look at the current state machine and decide if 
//  the state machine needs immidiate control for further
//  state machine processing. Called by the check function
//  of heartbeat-source of the main event loop.
int
ccm_need_control(void *data)
{
	ccm_info_t *info =  (ccm_info_t *)((ccm_t *)data)->info;

	if(leave_any() || 
		CCM_GET_STATE(info) != CCM_STATE_JOINED)
			return TRUE;
	return FALSE;
}

//  look at the current state machine and decide if 
//  the state machine needs immidiate control for further
//  state machine processing. Called by the check function
//  of heartbeat-source of the main event loop.
int
ccm_take_control(void *data)
{
	ccm_info_t *info =  (ccm_info_t *)((ccm_t *)data)->info;
	ll_cluster_t *hbfd = (ll_cluster_t *)((ccm_t *)data)->hbfd;
	static char client_flag=0;

	int ret = ccm_control_process(info, hbfd);

	
	if(!client_flag) {
		client_llm_init(CCM_GET_LLM(info));
		client_flag=1;
	}

	return ret;
}

int
ccm_get_fd(void *data)
{
	ll_cluster_t *hbfd = (ll_cluster_t *)((ccm_t *)data)->hbfd;

	return hbfd->llc_ops->inputfd(hbfd);
}

void *
ccm_initialize()
{
	unsigned	fmask;
	const char *	node;
	const char *	status;
	char 		hname[256];
	size_t		hlen = 256;
	llm_info_t 	*llm;
	ccm_info_t 	*global_info;
	ll_cluster_t*	hb_fd;
	ccm_t		*ccmret;


	(void)_heartbeat_h_Id;
	(void)_ha_msg_h_Id;

	system("clear");
	fprintf(stderr, "========================== Starting CCM ===="
			"======================\n");

	if(gethostname(hname,hlen) != 0) {
		fprintf(stderr, "gethostname() failed\n");
		return NULL;
	}

	hb_fd = ll_cluster_new("heartbeat");
	
	fprintf(stderr, "PID=%d\n", getpid());
	fprintf(stderr, "Hostname: %s\n", hname);

	fprintf(stderr, "Signing in with Heartbeat\n");
	if (hb_fd->llc_ops->signon(hb_fd, "ccm")!= HA_OK) {
		fprintf(stderr, "Cannot sign on with heartbeat\n");
		fprintf(stderr, "REASON: %s\n", hb_fd->llc_ops->errmsg(hb_fd));
		return NULL;
	}

	if((global_info = (ccm_info_t *)g_malloc(sizeof(ccm_info_t))) == NULL){
		fprintf(stderr, "Cannot allocate memory \n");
		return NULL;
	}

	if((ccmret = (ccm_t *)g_malloc(sizeof(ccm_t))) == NULL){
		fprintf(stderr, "Cannot allocate memory \n");
		return NULL;
	}

	if (hb_fd->llc_ops->set_nstatus_callback(hb_fd, nodelist_update, global_info) 
					!=HA_OK){
		fprintf(stderr, "Cannot set node status callback\n");
		fprintf(stderr, "REASON: %s\n", hb_fd->llc_ops->errmsg(hb_fd));
		return NULL;
	}

	if (hb_fd->llc_ops->set_ifstatus_callback(hb_fd, LinkStatus, NULL)
					!=HA_OK){
		fprintf(stderr, "Cannot set if status callback\n");
		fprintf(stderr, "REASON: %s\n", hb_fd->llc_ops->errmsg(hb_fd));
		return NULL;
	}
	
	fmask = LLC_FILTER_DEFAULT;
	if (hb_fd->llc_ops->setfmode(hb_fd, fmask) != HA_OK) {
		fprintf(stderr, "Cannot set filter mode\n");
		fprintf(stderr, "REASON: %s\n", hb_fd->llc_ops->errmsg(hb_fd));
		return NULL;
	}


	fprintf(stderr, "======================= Starting  Node Walk =="
			"=====================\n");
	if (hb_fd->llc_ops->init_nodewalk(hb_fd) != HA_OK) {
		fprintf(stderr, "Cannot start node walk\n");
		fprintf(stderr, "REASON: %s\n", hb_fd->llc_ops->errmsg(hb_fd));
		return NULL;
	}

	/* ccm */
	llm = CCM_GET_LLM((global_info));
	llm_init(llm);
	while((node = hb_fd->llc_ops->nextnode(hb_fd))!= NULL) {
		status =  hb_fd->llc_ops->node_status(hb_fd, node);
		fprintf(stderr, "Cluster node: %s: status: %s\n", node
		,	status);
		
		/* add the node to the low level membership list */
		llm_add(llm, node, status, hname);

	}
	llm_end(llm);

	if (hb_fd->llc_ops->end_nodewalk(hb_fd) != HA_OK) {
		fprintf(stderr, "Cannot end node walk\n");
		fprintf(stderr, "REASON: %s\n", hb_fd->llc_ops->errmsg(hb_fd));
		return NULL;
	}

	fprintf(stderr, "======================== Ending  Node Walk ======"
			"==================\n");
	fprintf(stderr, "Total # of Nodes in the Cluster: %d\n", 
						LLM_GET_NODECOUNT(llm));
	
	if (hb_fd->llc_ops->setmsgsignal(hb_fd, 0) != HA_OK) {
		fprintf(stderr, "Cannot set message signal\n");
		fprintf(stderr, "REASON: %s\n", hb_fd->llc_ops->errmsg(hb_fd));
		return NULL;
	}

	siginterrupt(SIGTERM, 1);

	ccm_control_init(global_info);

	ccmret->info = global_info;
	ccmret->hbfd = hb_fd;
	return  (void*)ccmret;
}
