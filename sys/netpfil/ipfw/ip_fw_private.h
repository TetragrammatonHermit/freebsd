/*-
 * Copyright (c) 2002-2009 Luigi Rizzo, Universita` di Pisa
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _IPFW2_PRIVATE_H
#define _IPFW2_PRIVATE_H

/*
 * Internal constants and data structures used by ipfw components
 * and not meant to be exported outside the kernel.
 */

#ifdef _KERNEL

/*
 * For platforms that do not have SYSCTL support, we wrap the
 * SYSCTL_* into a function (one per file) to collect the values
 * into an array at module initialization. The wrapping macros,
 * SYSBEGIN() and SYSEND, are empty in the default case.
 */
#ifndef SYSBEGIN
#define SYSBEGIN(x)
#endif
#ifndef SYSEND
#define SYSEND
#endif

/* Return values from ipfw_chk() */
enum {
	IP_FW_PASS = 0,
	IP_FW_DENY,
	IP_FW_DIVERT,
	IP_FW_TEE,
	IP_FW_DUMMYNET,
	IP_FW_NETGRAPH,
	IP_FW_NGTEE,
	IP_FW_NAT,
	IP_FW_REASS,
};

/*
 * Structure for collecting parameters to dummynet for ip6_output forwarding
 */
struct _ip6dn_args {
       struct ip6_pktopts *opt_or;
       struct route_in6 ro_or;
       int flags_or;
       struct ip6_moptions *im6o_or;
       struct ifnet *origifp_or;
       struct ifnet *ifp_or;
       struct sockaddr_in6 dst_or;
       u_long mtu_or;
       struct route_in6 ro_pmtu_or;
};


/*
 * Arguments for calling ipfw_chk() and dummynet_io(). We put them
 * all into a structure because this way it is easier and more
 * efficient to pass variables around and extend the interface.
 */
struct ip_fw_args {
	struct mbuf	*m;		/* the mbuf chain		*/
	struct ifnet	*oif;		/* output interface		*/
	struct sockaddr_in *next_hop;	/* forward address		*/
	struct sockaddr_in6 *next_hop6; /* ipv6 forward address		*/

	/*
	 * On return, it points to the matching rule.
	 * On entry, rule.slot > 0 means the info is valid and
	 * contains the starting rule for an ipfw search.
	 * If chain_id == chain->id && slot >0 then jump to that slot.
	 * Otherwise, we locate the first rule >= rulenum:rule_id
	 */
	struct ipfw_rule_ref rule;	/* match/restart info		*/

	struct ether_header *eh;	/* for bridged packets		*/

	struct ipfw_flow_id f_id;	/* grabbed from IP header	*/
	//uint32_t	cookie;		/* a cookie depending on rule action */
	struct inpcb	*inp;

	struct _ip6dn_args	dummypar; /* dummynet->ip6_output */
	struct sockaddr_in hopstore;	/* store here if cannot use a pointer */
};

MALLOC_DECLARE(M_IPFW);

/*
 * Hooks sometime need to know the direction of the packet
 * (divert, dummynet, netgraph, ...)
 * We use a generic definition here, with bit0-1 indicating the
 * direction, bit 2 indicating layer2 or 3, bit 3-4 indicating the
 * specific protocol
 * indicating the protocol (if necessary)
 */
enum {
	DIR_MASK =	0x3,
	DIR_OUT =	0,
	DIR_IN =	1,
	DIR_FWD =	2,
	DIR_DROP =	3,
	PROTO_LAYER2 =	0x4, /* set for layer 2 */
	/* PROTO_DEFAULT = 0, */
	PROTO_IPV4 =	0x08,
	PROTO_IPV6 =	0x10,
	PROTO_IFB =	0x0c, /* layer2 + ifbridge */
   /*	PROTO_OLDBDG =	0x14, unused, old bridge */
};

/* wrapper for freeing a packet, in case we need to do more work */
#ifndef FREE_PKT
#if defined(__linux__) || defined(_WIN32)
#define FREE_PKT(m)	netisr_dispatch(-1, m)
#else
#define FREE_PKT(m)	m_freem(m)
#endif
#endif /* !FREE_PKT */

/*
 * Function definitions.
 */

/* attach (arg = 1) or detach (arg = 0) hooks */
int ipfw_attach_hooks(int);
#ifdef NOTYET
void ipfw_nat_destroy(void);
#endif

/* In ip_fw_log.c */
struct ip;
struct ip_fw_chain;
void ipfw_log_bpf(int);
void ipfw_log(struct ip_fw_chain *chain, struct ip_fw *f, u_int hlen,
    struct ip_fw_args *args, struct mbuf *m, struct ifnet *oif,
    u_short offset, uint32_t tablearg, struct ip *ip);
VNET_DECLARE(u_int64_t, norule_counter);
#define	V_norule_counter	VNET(norule_counter)
VNET_DECLARE(int, verbose_limit);
#define	V_verbose_limit		VNET(verbose_limit)

/* In ip_fw_dynamic.c */

enum { /* result for matching dynamic rules */
	MATCH_REVERSE = 0,
	MATCH_FORWARD,
	MATCH_NONE,
	MATCH_UNKNOWN,
};

/*
 * The lock for dynamic rules is only used once outside the file,
 * and only to release the result of lookup_dyn_rule().
 * Eventually we may implement it with a callback on the function.
 */
struct ip_fw_chain;
struct sockopt_data;
int ipfw_is_dyn_rule(struct ip_fw *rule);
void ipfw_expire_dyn_rules(struct ip_fw_chain *, ipfw_range_tlv *);
void ipfw_dyn_unlock(ipfw_dyn_rule *q);

struct tcphdr;
struct mbuf *ipfw_send_pkt(struct mbuf *, struct ipfw_flow_id *,
    u_int32_t, u_int32_t, int);
int ipfw_install_state(struct ip_fw_chain *chain, struct ip_fw *rule,
    ipfw_insn_limit *cmd, struct ip_fw_args *args, uint32_t tablearg);
ipfw_dyn_rule *ipfw_lookup_dyn_rule(struct ipfw_flow_id *pkt,
	int *match_direction, struct tcphdr *tcp);
void ipfw_remove_dyn_children(struct ip_fw *rule);
void ipfw_get_dynamic(struct ip_fw_chain *chain, char **bp, const char *ep);
int ipfw_dump_states(struct ip_fw_chain *chain, struct sockopt_data *sd);

void ipfw_dyn_init(struct ip_fw_chain *);	/* per-vnet initialization */
void ipfw_dyn_uninit(int);	/* per-vnet deinitialization */
int ipfw_dyn_len(void);
int ipfw_dyn_get_count(void);

/* common variables */
VNET_DECLARE(int, fw_one_pass);
#define	V_fw_one_pass		VNET(fw_one_pass)

VNET_DECLARE(int, fw_verbose);
#define	V_fw_verbose		VNET(fw_verbose)

VNET_DECLARE(struct ip_fw_chain, layer3_chain);
#define	V_layer3_chain		VNET(layer3_chain)

VNET_DECLARE(int, ipfw_vnet_ready);
#define	V_ipfw_vnet_ready	VNET(ipfw_vnet_ready)

VNET_DECLARE(u_int32_t, set_disable);
#define	V_set_disable		VNET(set_disable)

VNET_DECLARE(int, autoinc_step);
#define V_autoinc_step		VNET(autoinc_step)

VNET_DECLARE(unsigned int, fw_tables_max);
#define V_fw_tables_max		VNET(fw_tables_max)

VNET_DECLARE(unsigned int, fw_tables_sets);
#define V_fw_tables_sets	VNET(fw_tables_sets)

struct tables_config;

#ifdef _KERNEL
/*
 * Here we have the structure representing an ipfw rule.
 *
 * It starts with a general area 
 * followed by an array of one or more instructions, which the code
 * accesses as an array of 32-bit values.
 *
 * Given a rule pointer  r:
 *
 *  r->cmd		is the start of the first instruction.
 *  ACTION_PTR(r)	is the start of the first action (things to do
 *			once a rule matched).
 */

struct ip_fw {
	uint16_t	act_ofs;	/* offset of action in 32-bit units */
	uint16_t	cmd_len;	/* # of 32-bit words in cmd	*/
	uint16_t	rulenum;	/* rule number			*/
	uint8_t		set;		/* rule set (0..31)		*/
	uint8_t		flags;		/* currently unused		*/
	counter_u64_t	cntr;		/* Pointer to rule counters	*/
	uint32_t	timestamp;	/* tv_sec of last match		*/
	uint32_t	id;		/* rule id			*/
	uint32_t	cached_id;	/* used by jump_fast		*/
	uint32_t	cached_pos;	/* used by jump_fast		*/

	ipfw_insn	cmd[1];		/* storage for commands		*/
};

#define	IPFW_RULE_CNTR_SIZE	(2 * sizeof(counter_u64_t))

#endif

struct ip_fw_chain {
	struct ip_fw	**map;		/* array of rule ptrs to ease lookup */
	uint32_t	id;		/* ruleset id */
	int		n_rules;	/* number of static rules */
	LIST_HEAD(nat_list, cfg_nat) nat;       /* list of nat entries */
	void		*tablestate;	/* runtime table info */
	void		*valuestate;	/* runtime table value info */
	int		*idxmap;	/* skipto array of rules */
#if defined( __linux__ ) || defined( _WIN32 )
	spinlock_t rwmtx;
#else
	struct rmlock	rwmtx;
#endif
	int		static_len;	/* total len of static rules (v0) */
	uint32_t	gencnt;		/* NAT generation count */
	struct ip_fw	*default_rule;
	struct tables_config *tblcfg;	/* tables module data */
	void		*ifcfg;		/* interface module data */
	int		*idxmap_back;	/* standby skipto array of rules */
#if defined( __linux__ ) || defined( _WIN32 )
	spinlock_t uh_lock;
#else
	struct rwlock	uh_lock;	/* lock for upper half */
#endif
};

/* 64-byte structure representing multi-field table value */
struct table_value {
	uint32_t	tag;		/* O_TAG/O_TAGGED */
	uint32_t	pipe;		/* O_PIPE/O_QUEUE */
	uint16_t	divert;		/* O_DIVERT/O_TEE */
	uint16_t	skipto;		/* skipto, CALLRET */
	uint32_t	netgraph;	/* O_NETGRAPH/O_NGTEE */
	uint32_t	fib;		/* O_SETFIB */
	uint32_t	nat;		/* O_NAT */
	uint32_t	nh4;
	uint8_t		dscp;
	uint8_t		spare0[3];
	/* -- 32 bytes -- */
	struct in6_addr	nh6;
	uint32_t	limit;		/* O_LIMIT */
	uint32_t	spare1;
	uint64_t	refcnt;		/* Number of references */
};

struct namedobj_instance;

struct named_object {
	TAILQ_ENTRY(named_object)	nn_next;	/* namehash */
	TAILQ_ENTRY(named_object)	nv_next;	/* valuehash */
	char			*name;	/* object name */
	uint8_t			type;	/* object type */
	uint8_t			compat;	/* Object name is number */
	uint16_t		kidx;	/* object kernel index */
	uint16_t		uidx;	/* userland idx for compat records */
	uint32_t		set;	/* set object belongs to */
	uint32_t		refcnt;	/* number of references */
};
TAILQ_HEAD(namedobjects_head, named_object);

struct sockopt;	/* used by tcp_var.h */
struct sockopt_data {
	caddr_t		kbuf;		/* allocated buffer */
	size_t		ksize;		/* given buffer size */
	size_t		koff;		/* data already used */
	size_t		kavail;		/* number of bytes available */
	size_t		ktotal;		/* total bytes pushed */
	struct sockopt	*sopt;		/* socket data */
	caddr_t		sopt_val;	/* sopt user buffer */
	size_t		valsize;	/* original data size */
};

struct ipfw_ifc;

typedef void (ipfw_ifc_cb)(struct ip_fw_chain *ch, void *cbdata,
    uint16_t ifindex);

struct ipfw_iface {
	struct named_object	no;
	char ifname[64];
	int resolved;
	uint16_t ifindex;
	uint16_t spare;
	uint64_t gencnt;
	TAILQ_HEAD(, ipfw_ifc)	consumers;
};

struct ipfw_ifc {
	TAILQ_ENTRY(ipfw_ifc)	next;
	struct ipfw_iface	*iface;
	ipfw_ifc_cb		*cb;
	void			*cbdata;
};

/* Macro for working with various counters */
#define	IPFW_INC_RULE_COUNTER(_cntr, _bytes)	do {	\
	counter_u64_add((_cntr)->cntr, 1);		\
	counter_u64_add((_cntr)->cntr + 1, _bytes);	\
	if ((_cntr)->timestamp != time_uptime)		\
		(_cntr)->timestamp = time_uptime;	\
	} while (0)

#define	IPFW_INC_DYN_COUNTER(_cntr, _bytes)	do {		\
	(_cntr)->pcnt++;				\
	(_cntr)->bcnt += _bytes;			\
	} while (0)

#define	IPFW_ZERO_RULE_COUNTER(_cntr) do {		\
	counter_u64_zero((_cntr)->cntr);		\
	counter_u64_zero((_cntr)->cntr + 1);		\
	(_cntr)->timestamp = 0;				\
	} while (0)

#define	IPFW_ZERO_DYN_COUNTER(_cntr) do {		\
	(_cntr)->pcnt = 0;				\
	(_cntr)->bcnt = 0;				\
	} while (0)

#define	TARG_VAL(ch, k, f)	((struct table_value *)((ch)->valuestate))[k].f
#define	IP_FW_ARG_TABLEARG(ch, a, f)	\
	(((a) == IP_FW_TARG) ? TARG_VAL(ch, tablearg, f) : (a))
/*
 * The lock is heavily used by ip_fw2.c (the main file) and ip_fw_nat.c
 * so the variable and the macros must be here.
 */

#if defined( __linux__ ) || defined( _WIN32 )
#define	IPFW_LOCK_INIT(_chain) do {			\
	rw_init(&(_chain)->rwmtx, "IPFW static rules");	\
	rw_init(&(_chain)->uh_lock, "IPFW UH lock");	\
	} while (0)

#define	IPFW_LOCK_DESTROY(_chain) do {			\
	rw_destroy(&(_chain)->rwmtx);			\
	rw_destroy(&(_chain)->uh_lock);			\
	} while (0)

#define	IPFW_RLOCK_ASSERT(_chain)	rw_assert(&(_chain)->rwmtx, RA_RLOCKED)
#define	IPFW_WLOCK_ASSERT(_chain)	rw_assert(&(_chain)->rwmtx, RA_WLOCKED)

#define	IPFW_RLOCK_TRACKER
#define	IPFW_RLOCK(p)			rw_rlock(&(p)->rwmtx)
#define	IPFW_RUNLOCK(p)			rw_runlock(&(p)->rwmtx)
#define	IPFW_WLOCK(p)			rw_wlock(&(p)->rwmtx)
#define	IPFW_WUNLOCK(p)			rw_wunlock(&(p)->rwmtx)
#define	IPFW_PF_RLOCK(p)		IPFW_RLOCK(p)
#define	IPFW_PF_RUNLOCK(p)		IPFW_RUNLOCK(p)
#else /* FreeBSD */
#define	IPFW_LOCK_INIT(_chain) do {			\
	rm_init(&(_chain)->rwmtx, "IPFW static rules");	\
	rw_init(&(_chain)->uh_lock, "IPFW UH lock");	\
	} while (0)

#define	IPFW_LOCK_DESTROY(_chain) do {			\
	rm_destroy(&(_chain)->rwmtx);			\
	rw_destroy(&(_chain)->uh_lock);			\
	} while (0)

#define	IPFW_RLOCK_ASSERT(_chain)	rm_assert(&(_chain)->rwmtx, RA_RLOCKED)
#define	IPFW_WLOCK_ASSERT(_chain)	rm_assert(&(_chain)->rwmtx, RA_WLOCKED)

#define	IPFW_RLOCK_TRACKER		struct rm_priotracker _tracker
#define	IPFW_RLOCK(p)			rm_rlock(&(p)->rwmtx, &_tracker)
#define	IPFW_RUNLOCK(p)			rm_runlock(&(p)->rwmtx, &_tracker)
#define	IPFW_WLOCK(p)			rm_wlock(&(p)->rwmtx)
#define	IPFW_WUNLOCK(p)			rm_wunlock(&(p)->rwmtx)
#define	IPFW_PF_RLOCK(p)		IPFW_RLOCK(p)
#define	IPFW_PF_RUNLOCK(p)		IPFW_RUNLOCK(p)
#endif

#define	IPFW_UH_RLOCK_ASSERT(_chain)	rw_assert(&(_chain)->uh_lock, RA_RLOCKED)
#define	IPFW_UH_WLOCK_ASSERT(_chain)	rw_assert(&(_chain)->uh_lock, RA_WLOCKED)

#define IPFW_UH_RLOCK(p) rw_rlock(&(p)->uh_lock)
#define IPFW_UH_RUNLOCK(p) rw_runlock(&(p)->uh_lock)
#define IPFW_UH_WLOCK(p) rw_wlock(&(p)->uh_lock)
#define IPFW_UH_WUNLOCK(p) rw_wunlock(&(p)->uh_lock)

struct obj_idx {
	uint16_t	uidx;	/* internal index supplied by userland */
	uint16_t	kidx;	/* kernel object index */
	uint16_t	off;	/* tlv offset from rule end in 4-byte words */
	uint8_t		spare;
	uint8_t		type;	/* object type within its category */
};

struct rule_check_info {
	uint16_t	flags;		/* rule-specific check flags */
	uint16_t	table_opcodes;	/* count of opcodes referencing table */
	uint16_t	urule_numoff;	/* offset of rulenum in bytes */
	uint8_t		version;	/* rule version */
	uint8_t		spare;
	ipfw_obj_ctlv	*ctlv;		/* name TLV containter */
	struct ip_fw	*krule;		/* resulting rule pointer */
	caddr_t		urule;		/* original rule pointer */
	struct obj_idx	obuf[8];	/* table references storage */
};

/* Legacy interface support */
/*
 * FreeBSD 8 export rule format
 */
struct ip_fw_rule0 {
	struct ip_fw	*x_next;	/* linked list of rules		*/
	struct ip_fw	*next_rule;	/* ptr to next [skipto] rule	*/
	/* 'next_rule' is used to pass up 'set_disable' status		*/

	uint16_t	act_ofs;	/* offset of action in 32-bit units */
	uint16_t	cmd_len;	/* # of 32-bit words in cmd	*/
	uint16_t	rulenum;	/* rule number			*/
	uint8_t		set;		/* rule set (0..31)		*/
	uint8_t		_pad;		/* padding			*/
	uint32_t	id;		/* rule id */

	/* These fields are present in all rules.			*/
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/

	ipfw_insn	cmd[1];		/* storage for commands		*/
};

struct ip_fw_bcounter0 {
	uint64_t	pcnt;		/* Packet counter		*/
	uint64_t	bcnt;		/* Byte counter			*/
	uint32_t	timestamp;	/* tv_sec of last match		*/
};

/* Kernel rule length */
/*
 * RULE _K_ SIZE _V_ ->
 * get kernel size from userland rool version _V_.
 * RULE _U_ SIZE _V_ ->
 * get user size version _V_ from kernel rule
 * RULESIZE _V_ ->
 * get user size rule length 
 */
/* FreeBSD8 <> current kernel format */
#define	RULEUSIZE0(r)	(sizeof(struct ip_fw_rule0) + (r)->cmd_len * 4 - 4)
#define	RULEKSIZE0(r)	roundup2((sizeof(struct ip_fw) + (r)->cmd_len*4 - 4), 8)
/* FreeBSD11 <> current kernel format */
#define	RULEUSIZE1(r)	(roundup2(sizeof(struct ip_fw_rule) + \
    (r)->cmd_len * 4 - 4, 8))
#define	RULEKSIZE1(r)	roundup2((sizeof(struct ip_fw) + (r)->cmd_len*4 - 4), 8)


/* In ip_fw_iface.c */
int ipfw_iface_init(void);
void ipfw_iface_destroy(void);
void vnet_ipfw_iface_destroy(struct ip_fw_chain *ch);
int ipfw_iface_ref(struct ip_fw_chain *ch, char *name,
    struct ipfw_ifc *ic);
void ipfw_iface_unref(struct ip_fw_chain *ch, struct ipfw_ifc *ic);
void ipfw_iface_add_notify(struct ip_fw_chain *ch, struct ipfw_ifc *ic);
void ipfw_iface_del_notify(struct ip_fw_chain *ch, struct ipfw_ifc *ic);

/* In ip_fw_sockopt.c */
void ipfw_init_skipto_cache(struct ip_fw_chain *chain);
void ipfw_destroy_skipto_cache(struct ip_fw_chain *chain);
int ipfw_find_rule(struct ip_fw_chain *chain, uint32_t key, uint32_t id);
int ipfw_ctl3(struct sockopt *sopt);
int ipfw_chk(struct ip_fw_args *args);
void ipfw_reap_add(struct ip_fw_chain *chain, struct ip_fw **head,
    struct ip_fw *rule);
void ipfw_reap_rules(struct ip_fw *head);
void ipfw_init_counters(void);
void ipfw_destroy_counters(void);
struct ip_fw *ipfw_alloc_rule(struct ip_fw_chain *chain, size_t rulesize);
int ipfw_match_range(struct ip_fw *rule, ipfw_range_tlv *rt);

typedef int (sopt_handler_f)(struct ip_fw_chain *ch,
    ip_fw3_opheader *op3, struct sockopt_data *sd);
struct ipfw_sopt_handler {
	uint16_t	opcode;
	uint8_t		version;
	uint8_t		dir;
	sopt_handler_f	*handler;
	uint64_t	refcnt;
};
#define	HDIR_SET	0x01	/* Handler is used to set some data */
#define	HDIR_GET	0x02	/* Handler is used to retrieve data */
#define	HDIR_BOTH	HDIR_GET|HDIR_SET

void ipfw_init_sopt_handler(void);
void ipfw_destroy_sopt_handler(void);
void ipfw_add_sopt_handler(struct ipfw_sopt_handler *sh, size_t count);
int ipfw_del_sopt_handler(struct ipfw_sopt_handler *sh, size_t count);
caddr_t ipfw_get_sopt_space(struct sockopt_data *sd, size_t needed);
caddr_t ipfw_get_sopt_header(struct sockopt_data *sd, size_t needed);
#define	IPFW_ADD_SOPT_HANDLER(f, c)	do {	\
	if ((f) != 0) 				\
		ipfw_add_sopt_handler(c,	\
		    sizeof(c) / sizeof(c[0]));	\
	} while(0)
#define	IPFW_DEL_SOPT_HANDLER(l, c)	do {	\
	if ((l) != 0) 				\
		ipfw_del_sopt_handler(c,	\
		    sizeof(c) / sizeof(c[0]));	\
	} while(0)

typedef void (objhash_cb_t)(struct namedobj_instance *ni, struct named_object *,
    void *arg);
typedef uint32_t (objhash_hash_f)(struct namedobj_instance *ni, void *key,
    uint32_t kopt);
typedef int (objhash_cmp_f)(struct named_object *no, void *key, uint32_t kopt);
struct namedobj_instance *ipfw_objhash_create(uint32_t items);
void ipfw_objhash_destroy(struct namedobj_instance *);
void ipfw_objhash_bitmap_alloc(uint32_t items, void **idx, int *pblocks);
void ipfw_objhash_bitmap_merge(struct namedobj_instance *ni,
    void **idx, int *blocks);
void ipfw_objhash_bitmap_swap(struct namedobj_instance *ni,
    void **idx, int *blocks);
void ipfw_objhash_bitmap_free(void *idx, int blocks);
void ipfw_objhash_set_hashf(struct namedobj_instance *ni, objhash_hash_f *f);
struct named_object *ipfw_objhash_lookup_name(struct namedobj_instance *ni,
    uint32_t set, char *name);
struct named_object *ipfw_objhash_lookup_kidx(struct namedobj_instance *ni,
    uint16_t idx);
int ipfw_objhash_same_name(struct namedobj_instance *ni, struct named_object *a,
    struct named_object *b);
void ipfw_objhash_add(struct namedobj_instance *ni, struct named_object *no);
void ipfw_objhash_del(struct namedobj_instance *ni, struct named_object *no);
uint32_t ipfw_objhash_count(struct namedobj_instance *ni);
void ipfw_objhash_foreach(struct namedobj_instance *ni, objhash_cb_t *f,
    void *arg);
int ipfw_objhash_free_idx(struct namedobj_instance *ni, uint16_t idx);
int ipfw_objhash_alloc_idx(void *n, uint16_t *pidx);
void ipfw_objhash_set_funcs(struct namedobj_instance *ni,
    objhash_hash_f *hash_f, objhash_cmp_f *cmp_f);

/* In ip_fw_table.c */
struct table_info;

typedef int (table_lookup_t)(struct table_info *ti, void *key, uint32_t keylen,
    uint32_t *val);

int ipfw_lookup_table(struct ip_fw_chain *ch, uint16_t tbl, in_addr_t addr,
    uint32_t *val);
int ipfw_lookup_table_extended(struct ip_fw_chain *ch, uint16_t tbl, uint16_t plen,
    void *paddr, uint32_t *val);
int ipfw_init_tables(struct ip_fw_chain *ch, int first);
int ipfw_resize_tables(struct ip_fw_chain *ch, unsigned int ntables);
int ipfw_switch_tables_namespace(struct ip_fw_chain *ch, unsigned int nsets);
void ipfw_destroy_tables(struct ip_fw_chain *ch, int last);

/* In ip_fw_nat.c -- XXX to be moved to ip_var.h */

extern struct cfg_nat *(*lookup_nat_ptr)(struct nat_list *, int);

typedef int ipfw_nat_t(struct ip_fw_args *, struct cfg_nat *, struct mbuf *);
typedef int ipfw_nat_cfg_t(struct sockopt *);

VNET_DECLARE(int, ipfw_nat_ready);
#define	V_ipfw_nat_ready	VNET(ipfw_nat_ready)
#define	IPFW_NAT_LOADED	(V_ipfw_nat_ready)

extern ipfw_nat_t *ipfw_nat_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_cfg_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_del_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_get_cfg_ptr;
extern ipfw_nat_cfg_t *ipfw_nat_get_log_ptr;

#endif /* _KERNEL */
#endif /* _IPFW2_PRIVATE_H */
