/*-
 * Copyright (c) 2012 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Edward Tomasz Napierala under sponsorship
 * from the FreeBSD Foundation.
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

#ifndef CTLD_H
#define	CTLD_H

#include <sys/queue.h>
#ifdef ICL_KERNEL_PROXY
#include <sys/types.h>
#endif
#include <sys/socket.h>
#include <stdbool.h>
#include <libutil.h>
#include <openssl/md5.h>

#define	DEFAULT_CONFIG_PATH		"/etc/ctl.conf"
#define	DEFAULT_PIDFILE			"/var/run/ctld.pid"
#define	DEFAULT_BLOCKSIZE		512

#define	MAX_NAME_LEN			223
#define	MAX_DATA_SEGMENT_LENGTH		(128 * 1024)
#define	MAX_BURST_LENGTH		16776192

struct auth {
	TAILQ_ENTRY(auth)		a_next;
	struct auth_group		*a_auth_group;
	char				*a_user;
	char				*a_secret;
	char				*a_mutual_user;
	char				*a_mutual_secret;
};

struct auth_name {
	TAILQ_ENTRY(auth_name)		an_next;
	struct auth_group		*an_auth_group;
	char				*an_initator_name;
};

struct auth_portal {
	TAILQ_ENTRY(auth_portal)	ap_next;
	struct auth_group		*ap_auth_group;
	char				*ap_initator_portal;
	struct sockaddr_storage		ap_sa;
	int				ap_mask;
};

#define	AG_TYPE_UNKNOWN			0
#define	AG_TYPE_DENY			1
#define	AG_TYPE_NO_AUTHENTICATION	2
#define	AG_TYPE_CHAP			3
#define	AG_TYPE_CHAP_MUTUAL		4

struct auth_group {
	TAILQ_ENTRY(auth_group)		ag_next;
	struct conf			*ag_conf;
	char				*ag_name;
	struct target			*ag_target;
	int				ag_type;
	TAILQ_HEAD(, auth)		ag_auths;
	TAILQ_HEAD(, auth_name)		ag_names;
	TAILQ_HEAD(, auth_portal)	ag_portals;
};

struct portal {
	TAILQ_ENTRY(portal)		p_next;
	struct portal_group		*p_portal_group;
	bool				p_iser;
	char				*p_listen;
	struct addrinfo			*p_ai;
#ifdef ICL_KERNEL_PROXY
	int				p_id;
#endif

	TAILQ_HEAD(, target)		p_targets;
	int				p_socket;
};

#define	PG_FILTER_UNKNOWN		0
#define	PG_FILTER_NONE			1
#define	PG_FILTER_PORTAL		2
#define	PG_FILTER_PORTAL_NAME		3
#define	PG_FILTER_PORTAL_NAME_AUTH	4

struct portal_group {
	TAILQ_ENTRY(portal_group)	pg_next;
	struct conf			*pg_conf;
	char				*pg_name;
	struct auth_group		*pg_discovery_auth_group;
	int				pg_discovery_filter;
	bool				pg_unassigned;
	TAILQ_HEAD(, portal)		pg_portals;

	uint16_t			pg_tag;
};

struct lun_option {
	TAILQ_ENTRY(lun_option)		lo_next;
	struct lun			*lo_lun;
	char				*lo_name;
	char				*lo_value;
};

struct lun {
	TAILQ_ENTRY(lun)		l_next;
	TAILQ_HEAD(, lun_option)	l_options;
	struct target			*l_target;
	int				l_lun;
	char				*l_backend;
	int				l_blocksize;
	char				*l_device_id;
	char				*l_path;
	char				*l_serial;
	int64_t				l_size;

	int				l_ctl_lun;
};

struct target {
	TAILQ_ENTRY(target)		t_next;
	TAILQ_HEAD(, lun)		t_luns;
	struct conf			*t_conf;
	struct auth_group		*t_auth_group;
	struct portal_group		*t_portal_group;
	char				*t_name;
	char				*t_alias;
};

struct isns {
	TAILQ_ENTRY(isns)		i_next;
	struct conf			*i_conf;
	char				*i_addr;
	struct addrinfo			*i_ai;
};

struct conf {
	char				*conf_pidfile_path;
	TAILQ_HEAD(, target)		conf_targets;
	TAILQ_HEAD(, auth_group)	conf_auth_groups;
	TAILQ_HEAD(, portal_group)	conf_portal_groups;
	TAILQ_HEAD(, isns)		conf_isns;
	int				conf_isns_period;
	int				conf_isns_timeout;
	int				conf_debug;
	int				conf_timeout;
	int				conf_maxproc;

	uint16_t			conf_last_portal_group_tag;
#ifdef ICL_KERNEL_PROXY
	int				conf_portal_id;
#endif
	struct pidfh			*conf_pidfh;

	bool				conf_default_pg_defined;
	bool				conf_default_ag_defined;
	bool				conf_kernel_port_on;
};

#define	CONN_SESSION_TYPE_NONE		0
#define	CONN_SESSION_TYPE_DISCOVERY	1
#define	CONN_SESSION_TYPE_NORMAL	2

#define	CONN_DIGEST_NONE		0
#define	CONN_DIGEST_CRC32C		1

struct connection {
	struct portal		*conn_portal;
	struct target		*conn_target;
	int			conn_socket;
	int			conn_session_type;
	char			*conn_initiator_name;
	char			*conn_initiator_addr;
	char			*conn_initiator_alias;
	uint8_t			conn_initiator_isid[6];
	struct sockaddr_storage	conn_initiator_sa;
	uint32_t		conn_cmdsn;
	uint32_t		conn_statsn;
	size_t			conn_max_data_segment_length;
	size_t			conn_max_burst_length;
	int			conn_immediate_data;
	int			conn_header_digest;
	int			conn_data_digest;
	const char		*conn_user;
	struct chap		*conn_chap;
};

struct pdu {
	struct connection	*pdu_connection;
	struct iscsi_bhs	*pdu_bhs;
	char			*pdu_data;
	size_t			pdu_data_len;
};

#define	KEYS_MAX	1024

struct keys {
	char		*keys_names[KEYS_MAX];
	char		*keys_values[KEYS_MAX];
	char		*keys_data;
	size_t		keys_data_len;
};

#define	CHAP_CHALLENGE_LEN	1024

struct chap {
	unsigned char	chap_id;
	char		chap_challenge[CHAP_CHALLENGE_LEN];
	char		chap_response[MD5_DIGEST_LENGTH];
};

struct rchap {
	char		*rchap_secret;
	unsigned char	rchap_id;
	void		*rchap_challenge;
	size_t		rchap_challenge_len;
};

struct chap		*chap_new(void);
char			*chap_get_id(const struct chap *chap);
char			*chap_get_challenge(const struct chap *chap);
int			chap_receive(struct chap *chap, const char *response);
int			chap_authenticate(struct chap *chap,
			    const char *secret);
void			chap_delete(struct chap *chap);

struct rchap		*rchap_new(const char *secret);
int			rchap_receive(struct rchap *rchap,
			    const char *id, const char *challenge);
char			*rchap_get_response(struct rchap *rchap);
void			rchap_delete(struct rchap *rchap);

struct conf		*conf_new(void);
struct conf		*conf_new_from_file(const char *path);
struct conf		*conf_new_from_kernel(void);
void			conf_delete(struct conf *conf);
int			conf_verify(struct conf *conf);

struct auth_group	*auth_group_new(struct conf *conf, const char *name);
void			auth_group_delete(struct auth_group *ag);
struct auth_group	*auth_group_find(const struct conf *conf,
			    const char *name);
int			auth_group_set_type(struct auth_group *ag,
			    const char *type);

const struct auth	*auth_new_chap(struct auth_group *ag,
			    const char *user, const char *secret);
const struct auth	*auth_new_chap_mutual(struct auth_group *ag,
			    const char *user, const char *secret,
			    const char *user2, const char *secret2);
const struct auth	*auth_find(const struct auth_group *ag,
			    const char *user);

const struct auth_name	*auth_name_new(struct auth_group *ag,
			    const char *initiator_name);
bool			auth_name_defined(const struct auth_group *ag);
const struct auth_name	*auth_name_find(const struct auth_group *ag,
			    const char *initiator_name);
int			auth_name_check(const struct auth_group *ag,
			    const char *initiator_name);

const struct auth_portal	*auth_portal_new(struct auth_group *ag,
				    const char *initiator_portal);
bool			auth_portal_defined(const struct auth_group *ag);
const struct auth_portal	*auth_portal_find(const struct auth_group *ag,
				    const struct sockaddr_storage *sa);
int				auth_portal_check(const struct auth_group *ag,
				    const struct sockaddr_storage *sa);

struct portal_group	*portal_group_new(struct conf *conf, const char *name);
void			portal_group_delete(struct portal_group *pg);
struct portal_group	*portal_group_find(const struct conf *conf,
			    const char *name);
int			portal_group_add_listen(struct portal_group *pg,
			    const char *listen, bool iser);
int			portal_group_set_filter(struct portal_group *pg,
			    const char *filter);

int			isns_new(struct conf *conf, const char *addr);
void			isns_delete(struct isns *is);
void			isns_register(struct isns *isns, struct isns *oldisns);
void			isns_check(struct isns *isns);
void			isns_deregister(struct isns *isns);

struct target		*target_new(struct conf *conf, const char *name);
void			target_delete(struct target *target);
struct target		*target_find(struct conf *conf,
			    const char *name);

struct lun		*lun_new(struct target *target, int lun_id);
void			lun_delete(struct lun *lun);
struct lun		*lun_find(const struct target *target, int lun_id);
void			lun_set_backend(struct lun *lun, const char *value);
void			lun_set_blocksize(struct lun *lun, size_t value);
void			lun_set_device_id(struct lun *lun, const char *value);
void			lun_set_path(struct lun *lun, const char *value);
void			lun_set_serial(struct lun *lun, const char *value);
void			lun_set_size(struct lun *lun, size_t value);
void			lun_set_ctl_lun(struct lun *lun, uint32_t value);

struct lun_option	*lun_option_new(struct lun *lun,
			    const char *name, const char *value);
void			lun_option_delete(struct lun_option *clo);
struct lun_option	*lun_option_find(const struct lun *lun,
			    const char *name);
void			lun_option_set(struct lun_option *clo,
			    const char *value);

void			kernel_init(void);
int			kernel_lun_add(struct lun *lun);
int			kernel_lun_resize(struct lun *lun);
int			kernel_lun_remove(struct lun *lun);
void			kernel_handoff(struct connection *conn);
int			kernel_port_add(struct target *targ);
int			kernel_port_remove(struct target *targ);
void			kernel_capsicate(void);

#ifdef ICL_KERNEL_PROXY
void			kernel_listen(struct addrinfo *ai, bool iser,
			    int portal_id);
void			kernel_accept(int *connection_id, int *portal_id,
			    struct sockaddr *client_sa,
			    socklen_t *client_salen);
void			kernel_send(struct pdu *pdu);
void			kernel_receive(struct pdu *pdu);
#endif

struct keys		*keys_new(void);
void			keys_delete(struct keys *keys);
void			keys_load(struct keys *keys, const struct pdu *pdu);
void			keys_save(struct keys *keys, struct pdu *pdu);
const char		*keys_find(struct keys *keys, const char *name);
int			keys_find_int(struct keys *keys, const char *name);
void			keys_add(struct keys *keys,
			    const char *name, const char *value);
void			keys_add_int(struct keys *keys,
			    const char *name, int value);

struct pdu		*pdu_new(struct connection *conn);
struct pdu		*pdu_new_response(struct pdu *request);
void			pdu_delete(struct pdu *pdu);
void			pdu_receive(struct pdu *request);
void			pdu_send(struct pdu *response);

void			login(struct connection *conn);

void			discovery(struct connection *conn);

void			log_init(int level);
void			log_set_peer_name(const char *name);
void			log_set_peer_addr(const char *addr);
void			log_err(int, const char *, ...)
			    __dead2 __printflike(2, 3);
void			log_errx(int, const char *, ...)
			    __dead2 __printflike(2, 3);
void			log_warn(const char *, ...) __printflike(1, 2);
void			log_warnx(const char *, ...) __printflike(1, 2);
void			log_debugx(const char *, ...) __printflike(1, 2);

char			*checked_strdup(const char *);
bool			valid_iscsi_name(const char *name);
void			set_timeout(int timeout, int fatal);
bool			timed_out(void);

#endif /* !CTLD_H */
