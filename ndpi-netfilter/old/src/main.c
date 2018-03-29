/* 
 * main.c
 * Copyright (C) 2010-2012 G. Elian Gidoni <geg@gnu.org>
 *               2012 Ed Wildgoose <lists@wildgooses.com>
 *               2014 Humberto Jucá <betolj@gmail.com>
 * 
 * This file is part of nDPI, an open source deep packet inspection
 * library based on the PACE technology by ipoque GmbH
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the License.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/version.h>
#include <linux/notifier.h>
#include <linux/netfilter/x_tables.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/if_ether.h>
#include <linux/rbtree.h>
#include <linux/kref.h>
#include <linux/time.h>
#include <net/tcp.h>

#include <net/netfilter/nf_conntrack.h>
#include <net/netfilter/nf_conntrack_ecache.h>

#include "ndpi_main.h"
#include "xt_ndpi.h"


/* flow tracking */
struct osdpi_flow_node {
        struct rb_node node;
        struct nf_conn * ct;
        u_int64_t ndpi_timeout;  // detection timeout - detection 30s / connection 180s
        /* mark if done detecting flow proto - no more tries */
        u8 detection_completed;
	/* result only, not used for flow identification */
	u32 detected_protocol;
        /* last pointer assigned at run time */
	struct ndpi_flow_struct *ndpi_flow;
};

/* id tracking */
struct osdpi_id_node {
        struct rb_node node;
        struct kref refcnt;
	union nf_inet_addr ip;
        /* last pointer assigned at run time */
	struct ndpi_id_struct *ndpi_id;
};


u64 gc_interval_timeout = 0;
static u32 size_id_struct = 0;
static u32 size_flow_struct = 0;

static struct rb_root osdpi_flow_root = RB_ROOT;
static struct rb_root osdpi_id_root = RB_ROOT;

static struct kmem_cache *osdpi_flow_cache __read_mostly;
static struct kmem_cache *osdpi_id_cache __read_mostly;

static NDPI_PROTOCOL_BITMASK protocols_bitmask;
static atomic_t protocols_cnt[NDPI_LAST_IMPLEMENTED_PROTOCOL];
static u8 nfndpi_protocols_http[NDPI_LAST_IMPLEMENTED_PROTOCOL];

DEFINE_SPINLOCK(flow_lock);
DEFINE_SPINLOCK(id_lock);
DEFINE_SPINLOCK(ipq_lock);
DEFINE_SPINLOCK(gc_lock);


/* detection */
static struct ndpi_detection_module_struct *ndpi_struct = NULL;
static u32 detection_tick_resolution = 1000;

/* debug functions */

static void debug_printf(u32 protocol, void *id_struct,
                         ndpi_log_level_t log_level, const char *format, ...)
{
        /* do nothing */

        va_list args;
        va_start(args, format);
        switch (log_level)
        {
            case NDPI_LOG_ERROR: 
                vprintk(format, args);
                break;
            case NDPI_LOG_TRACE:
                vprintk(format, args);
                break;

            case NDPI_LOG_DEBUG:
                vprintk(format, args);
                break;
        }
        va_end(args);
}


static void *malloc_wrapper(unsigned long size)
{
	return kmalloc(size, GFP_KERNEL);
}


static void free_wrapper(void *freeable)
{
	kfree(freeable);
}


static struct osdpi_flow_node *
ndpi_flow_search(struct rb_root *root, struct nf_conn *ct)
{
        struct osdpi_flow_node *data;
  	struct rb_node *node = root->rb_node;

  	while (node) {
                data = rb_entry(node, struct osdpi_flow_node, node);

                if (ct < data->ct)
                        node = node->rb_left;
                else if (ct > data->ct)
                        node = node->rb_right;
                else
                        return data;
	}

	return NULL;
}


static int
ndpi_flow_insert(struct rb_root *root, struct osdpi_flow_node *data)
{
        struct osdpi_flow_node *this;
  	struct rb_node **new = &(root->rb_node), *parent = NULL;

  	while (*new) {
                this = rb_entry(*new, struct osdpi_flow_node, node);

		parent = *new;
  		if (data->ct < this->ct)
  			new = &((*new)->rb_left);
  		else if (data->ct > this->ct)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}
  	rb_link_node(&data->node, parent, new);
  	rb_insert_color(&data->node, root);

	return 1;
}


static struct osdpi_id_node *
ndpi_id_search(struct rb_root *root, union nf_inet_addr *ip)
{
        int res;
        struct osdpi_id_node *data;
  	struct rb_node *node = root->rb_node;

  	while (node) {
                data = rb_entry(node, struct osdpi_id_node, node);
		res = memcmp(ip, &data->ip, sizeof(union nf_inet_addr));

		if (res < 0)
  			node = node->rb_left;
		else if (res > 0)
  			node = node->rb_right;
		else
  			return data;
	}

	return NULL;
}


static int
ndpi_id_insert(struct rb_root *root, struct osdpi_id_node *data)
{
        int res;
        struct osdpi_id_node *this;
  	struct rb_node **new = &(root->rb_node), *parent = NULL;

  	while (*new) {
                this = rb_entry(*new, struct osdpi_id_node, node);
		res = memcmp(&data->ip, &this->ip, sizeof(union nf_inet_addr));

		parent = *new;
  		if (res < 0)
  			new = &((*new)->rb_left);
  		else if (res > 0)
  			new = &((*new)->rb_right);
  		else
  			return 0;
  	}

  	rb_link_node(&data->node, parent, new);
  	rb_insert_color(&data->node, root);

	return 1;
}


static void
ndpi_id_release(struct kref *kref)
{
        struct osdpi_id_node * id;

        id = container_of (kref, struct osdpi_id_node, refcnt);
        rb_erase(&id->node, &osdpi_id_root);
        kmem_cache_free (osdpi_id_cache, id);
	//kfree(id);
}


static struct osdpi_flow_node *
ndpi_alloc_flow (struct nf_conn * ct)
{
        struct osdpi_flow_node *flow;

	flow = kmem_cache_zalloc (osdpi_flow_cache, GFP_ATOMIC);
	//flow = kzalloc(sizeof(struct ndpi_flow_struct *) + size_flow_struct, GFP_KERNEL);

        if (flow == NULL) {
                pr_err("xt_ndpi: couldn't allocate new flow.\n");
                return NULL;
        }
	else {
	        flow->ct = ct;
	        flow->ndpi_flow = (struct ndpi_flow_struct *)
	                ((char*)&flow->ndpi_flow+sizeof(flow->ndpi_flow));
	        ndpi_flow_insert (&osdpi_flow_root, flow);
	}

        return flow;
}

static void
nfndpi_free_flow (struct nf_conn * ct, struct osdpi_flow_node * auxflow)
{
        struct osdpi_flow_node * flow;

	spin_lock_bh (&flow_lock);
	if (auxflow == NULL)
	        flow = ndpi_flow_search (&osdpi_flow_root, ct);
	else
		flow = auxflow;

        if (flow != NULL){
                rb_erase (&flow->node, &osdpi_flow_root);
	        kmem_cache_free (osdpi_flow_cache, flow);
                //kfree (flow);
        }
	spin_unlock_bh (&flow_lock);
}


static struct osdpi_id_node *
ndpi_alloc_id (union nf_inet_addr * ip)
{
	struct osdpi_id_node *id;

        id = kmem_cache_zalloc (osdpi_id_cache, GFP_ATOMIC);
	//id = kzalloc(sizeof(struct ndpi_id_struct *) + size_id_struct, GFP_KERNEL);

        if (id == NULL){
		pr_err("xt_ndpi: couldn't allocate new id.\n");
		return NULL;
	}
	else {
	        memcpy(&id->ip, ip, sizeof(union nf_inet_addr));
	        id->ndpi_id = (struct ndpi_id_struct *)
	                ((char*)&id->ndpi_id+sizeof(id->ndpi_id));
	        kref_init (&id->refcnt);
	        ndpi_id_insert (&osdpi_id_root, id);
	}

        return id;
}


static void
nfndpi_free_id (union nf_inet_addr * ip)
{
        struct osdpi_id_node *id;

	spin_lock_bh (&id_lock);
        id = ndpi_id_search (&osdpi_id_root, ip);
        if (id != NULL)
                kref_put (&id->refcnt, ndpi_id_release);
	spin_unlock_bh (&id_lock);
}


static void
ndpi_enable_protocols (const struct xt_ndpi_mtinfo*info)
{
        int i;

        for (i = 1; i <= NDPI_LAST_IMPLEMENTED_PROTOCOL; i++){
                if (NDPI_COMPARE_PROTOCOL_TO_BITMASK(info->flags, i) != 0){
		        spin_lock_bh (&ipq_lock);

			//Force http or ssl detection for webserver host requests
                        if (nfndpi_protocols_http[i]) {
				NDPI_ADD_PROTOCOL_TO_BITMASK(protocols_bitmask, NDPI_PROTOCOL_DNS);
				NDPI_ADD_PROTOCOL_TO_BITMASK(protocols_bitmask, NDPI_PROTOCOL_HTTP);
				NDPI_ADD_PROTOCOL_TO_BITMASK(protocols_bitmask, NDPI_PROTOCOL_SSL);
                        }
			atomic_inc(&protocols_cnt[i-1]);
			NDPI_ADD_PROTOCOL_TO_BITMASK(protocols_bitmask, i);
			ndpi_set_protocol_detection_bitmask2
	                        (ndpi_struct,&protocols_bitmask);

			spin_unlock_bh (&ipq_lock);
                }
        }
}


static void
ndpi_disable_protocols (const struct xt_ndpi_mtinfo*info)
{
        int i;

        for (i = 1; i <= NDPI_LAST_IMPLEMENTED_PROTOCOL; i++){
                if (NDPI_COMPARE_PROTOCOL_TO_BITMASK(info->flags, i) != 0){
		        spin_lock_bh (&ipq_lock);
                        if (atomic_dec_and_test(&protocols_cnt[i-1])){
                                NDPI_DEL_PROTOCOL_FROM_BITMASK(protocols_bitmask, i);
                                ndpi_set_protocol_detection_bitmask2
                                        (ndpi_struct, &protocols_bitmask);
                        }
		        spin_unlock_bh (&ipq_lock);
                }
        }
}


static void ndpi_kill_flow(struct nf_conn * ct, union nf_inet_addr *ipsrc, union nf_inet_addr *ipdst) {
	nfndpi_free_id (ipsrc);
	nfndpi_free_id (ipdst);
	nfndpi_free_flow(ct, NULL);
}


static void ndpi_gc_flow(void)
{
	struct nf_conn * ct;
        struct rb_node * next;
        struct osdpi_flow_node *flow;
	union nf_inet_addr *ipdst;

        u64 t1;
        struct timeval tv;

        do_gettimeofday(&tv);
        t1 = (uint64_t) tv.tv_sec;
        
	spin_lock_bh (&gc_lock);
	//pr_info ("xt_ndpi: call garbage collector.\n");
        next = rb_first(&osdpi_flow_root);
        while (next){
                flow = rb_entry(next, struct osdpi_flow_node, node);
                next = rb_next(&flow->node);
		if (flow && (t1 - flow->ndpi_timeout > 180)) {
	                ct = flow->ct;
			ipdst = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3;
			//pr_info ("xt_ndpi: deleted by garbage collector - proto %d - dst %pI4\n", ipdst, flow->detected_protocol);

			nfndpi_free_id (&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3);
			nfndpi_free_id (ipdst);
			nfndpi_free_flow(ct, flow);
		}
        }
	spin_unlock_bh (&gc_lock);
}


static u32
ndpi_process_packet(struct nf_conn * ct, const uint64_t time,
                      const struct iphdr *iph, uint16_t ipsize, const struct tcphdr *tcph)
{
	u32 proto = NDPI_PROTOCOL_UNKNOWN;
        union nf_inet_addr *ipsrc, *ipdst;
        struct osdpi_id_node *src, *dst;
        struct osdpi_flow_node *flow;

	u8 exist_flow=0;
        u64 t1;
        struct timeval tv;

        ipsrc = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3;
       	ipdst = &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3;

	spin_lock_bh (&flow_lock);
	flow = ndpi_flow_search (&osdpi_flow_root, ct);

	if (iph->protocol == IPPROTO_TCP) {
                if (tcph->syn) {
			spin_unlock_bh (&flow_lock);
			return proto;
		}
                else if ((tcph->fin || tcph->rst) && flow != NULL) {
			spin_unlock_bh (&flow_lock);
			ndpi_kill_flow(ct, ipsrc, ipdst);
			return proto;
		}
	}
	else if (iph->protocol == IPPROTO_ICMP) {
		spin_unlock_bh (&flow_lock);
		return NDPI_PROTOCOL_IP_ICMP;
	}
	else {
		if (nf_ct_is_dying(ct)) {
			spin_unlock_bh (&flow_lock);
			ndpi_kill_flow(ct, ipsrc, ipdst);
			return proto;
		}
	}

        do_gettimeofday(&tv);
        t1 = (uint64_t) tv.tv_sec;

        if (flow == NULL) {
		if (!gc_interval_timeout) gc_interval_timeout = t1;
		else {
			if (t1 - gc_interval_timeout > 59) {
				spin_unlock_bh (&flow_lock);
				ndpi_gc_flow();
				gc_interval_timeout = t1;
				spin_lock_bh (&flow_lock);
			}
		}

		flow = ndpi_alloc_flow(ct);
                if (flow == NULL) {
			spin_unlock_bh (&flow_lock);
			return NDPI_PROTOCOL_UNKNOWN;
		}
                else {
			/* Include flow timeouts */
			flow->ndpi_timeout = t1;  // 30s for DPI timeout  and 180 for connection
		        flow->detected_protocol = NDPI_PROTOCOL_UNKNOWN;
			flow->detection_completed = 0;
                }
        }
        else {
		/* Update timeouts */
		exist_flow=1;
		if (flow->detected_protocol) {
			proto = flow->detected_protocol;
			flow->ndpi_timeout = t1;
			//pr_info ("xt_ndpi: flow detected. %d\n", flow->detected_protocol);
			spin_unlock_bh (&flow_lock);
			return proto;
	        }
	        else if (!flow->detected_protocol && (t1 - flow->ndpi_timeout > 30)) {
			//pr_info ("xt_ndpi: expired.\n");
			spin_unlock_bh (&flow_lock);
			return NDPI_PROTOCOL_UNKNOWN;

	        }
	}

	/* Invalid DPI flow */
	if (flow->ndpi_flow == NULL) {
		spin_unlock_bh (&flow_lock);
		ndpi_kill_flow(ct, ipsrc, ipdst);
		return proto;
	}

	flow->ndpi_timeout = t1;
	spin_unlock_bh (&flow_lock);


        spin_lock_bh (&id_lock);
        src = ndpi_id_search (&osdpi_id_root, ipsrc);
	if (src == NULL) {
                src = ndpi_alloc_id(ipsrc);
                if (src == NULL) {
		        spin_unlock_bh (&id_lock);
			return proto;
		}
	}
	else if (!exist_flow) kref_get (&src->refcnt);

        dst = ndpi_id_search (&osdpi_id_root, ipdst);
	if (dst == NULL) {
                dst = ndpi_alloc_id(ipdst);
                if (dst == NULL) {
		        spin_unlock_bh (&id_lock);
			return proto;
		}
	}
	else if (!exist_flow) kref_get (&dst->refcnt);
        spin_unlock_bh (&id_lock);


        /* here the actual detection is performed */
        spin_lock_bh (&ipq_lock);
        proto = ndpi_detection_process_packet(ndpi_struct,flow->ndpi_flow,
                                                (uint8_t *) iph, ipsize, time,
                                                src->ndpi_id, dst->ndpi_id);

	/* set detected protocol */
        flow->detected_protocol = proto;
        if (proto > NDPI_LAST_IMPLEMENTED_PROTOCOL)
                proto = NDPI_PROTOCOL_UNKNOWN;
	else {
	        if (flow->detected_protocol != NDPI_PROTOCOL_UNKNOWN) {
			//pr_info ("xt_ndpi: proto detected. %d\n", flow->detected_protocol);
			if (flow->ndpi_flow->no_cache_protocol)
				ndpi_kill_flow(ct, ipsrc, ipdst);
			else {
				/* update timeouts */
				flow->ndpi_timeout = t1;
				flow->detection_completed = 1;

				/* reset detection */
				if (flow->ndpi_flow) memset(flow->ndpi_flow, 0, sizeof(*(flow->ndpi_flow)));
			}
	        }
	}
        spin_unlock_bh (&ipq_lock);

	return proto;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static bool 
ndpi_mt (const struct sk_buff *skb,
            const struct net_device *in,
            const struct net_device *out,
            const struct xt_match *match,
            const void *matchinfo,
            int offset,
            unsigned int protoff,
            bool *hotdrop)

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
static bool
ndpi_mt(const struct sk_buff *skb, const struct xt_match_param *par)
#else
static bool
ndpi_mt(const struct sk_buff *skb, struct xt_action_param *par)
#endif
{
	u32 proto = NDPI_PROTOCOL_UNKNOWN;
	u64 time;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	const struct xt_ndpi_mtinfo *info = matchinfo;
#else
	const struct xt_ndpi_mtinfo *info = par->matchinfo;
#endif

	enum ip_conntrack_info ctinfo;
	struct nf_conn * ct;
	struct timeval tv;
	struct sk_buff *linearized_skb = NULL;
	const struct sk_buff *skb_use = NULL;

        const struct iphdr *ip;
        const struct tcphdr *tcph;

	if (skb_is_nonlinear(skb)){
		linearized_skb = skb_copy(skb, GFP_ATOMIC);
		if (linearized_skb == NULL) {
			pr_info ("xt_ndpi: linearization failed.\n");
			return false;
		}
		skb_use = linearized_skb;
	} else {
		skb_use = skb;
	}


	ct = nf_ct_get (skb_use, &ctinfo);
	if (ct == NULL){
		if(linearized_skb != NULL)
			kfree_skb(linearized_skb);

		return false;
#if LINUX_VERSION_CODE < KERNEL_VERSION(3,0,0)
	} else if (nf_ct_is_untracked(skb)){
#else
	} else if (nf_ct_is_untracked(ct)){
#endif
		pr_info ("xt_ndpi: ignoring untracked sk_buff.\n");
		return false;               
	}


	/* process the packet */
        ip = ip_hdr(skb_use);
        tcph = (const void *)ip + ip_hdrlen(skb_use);

	do_gettimeofday(&tv);
	time = ((uint64_t) tv.tv_sec) * detection_tick_resolution +
		tv.tv_usec / (1000000 / detection_tick_resolution);

	/* reset for new packets and solve ct collisions */
	if (ctinfo == IP_CT_NEW)
		ndpi_kill_flow(ct, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3);

	proto = ndpi_process_packet(ct, time, ip_hdr(skb_use), skb_use->len, tcph);
	

	if(linearized_skb != NULL)
		kfree_skb(linearized_skb);

        if (NDPI_COMPARE_PROTOCOL_TO_BITMASK(info->flags,proto) != 0) {
		if(par->hotdrop)
			ndpi_kill_flow(ct, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.src.u3, &ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.u3);
                return true;
	}

        return false;
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static bool 
ndpi_mt_check(const char *tablename,
                 const void *ip,
                 const struct xt_match *match,
                 void *matchinfo,
                 unsigned int hook_mask)

{
	const struct xt_ndpi_mtinfo *info = matchinfo;


        if (NDPI_BITMASK_IS_ZERO(info->flags)) {
                pr_info("None selected protocol.\n");
                return -EINVAL;
        }

        ndpi_enable_protocols (info);

	return nf_ct_l3proto_try_module_get (match->family) == 0;
}

#elif LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
static bool
ndpi_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_ndpi_mtinfo *info = par->matchinfo;

        if (NDPI_BITMASK_IS_ZERO(info->flags)) {
                pr_info("None selected protocol.\n");
                return -EINVAL;
        }

        ndpi_enable_protocols (info);

	return nf_ct_l3proto_try_module_get (par->family) == 0;
}
#else
static int
ndpi_mt_check(const struct xt_mtchk_param *par)
{
	const struct xt_ndpi_mtinfo *info = par->matchinfo;

        if (NDPI_BITMASK_IS_ZERO(info->flags)) {
                pr_info("None selected protocol.\n");
                return -EINVAL;
        }

        ndpi_enable_protocols (info);

	return nf_ct_l3proto_try_module_get (par->family);
}
#endif


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
static void 
ndpi_mt_destroy (const struct xt_match *match, void *matchinfo)
{
	const struct xt_ndpi_mtinfo *info = matchinfo;

        ndpi_disable_protocols (info);
	nf_ct_l3proto_module_put (match->family);
}

#else
static void 
ndpi_mt_destroy (const struct xt_mtdtor_param *par)
{
	const struct xt_ndpi_mtinfo *info = par->matchinfo;

        ndpi_disable_protocols (info);
	nf_ct_l3proto_module_put (par->family);
}

#endif



static void ndpi_cleanup(void)
{
        struct rb_node * next;
        struct osdpi_id_node *id;
        struct osdpi_flow_node *flow;

        ndpi_exit_detection_module(ndpi_struct, free_wrapper);

        /* free all objects before destroying caches */
        next = rb_first(&osdpi_flow_root);
        while (next){
                flow = rb_entry(next, struct osdpi_flow_node, node);
                next = rb_next(&flow->node);

                rb_erase(&flow->node, &osdpi_flow_root);
	        kmem_cache_free (osdpi_flow_cache, flow);
        }
        kmem_cache_destroy (osdpi_flow_cache);
        
        next = rb_first(&osdpi_id_root);
        while (next){
                id = rb_entry(next, struct osdpi_id_node, node);
                next = rb_next(&id->node);
                rb_erase(&id->node, &osdpi_id_root);
                kmem_cache_free (osdpi_id_cache, id);
        }
        kmem_cache_destroy (osdpi_id_cache);
}


static struct xt_match
ndpi_mt_reg __read_mostly = {
	.name = "ndpi",
	.revision = 0,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
	.family = AF_INET,
#else
	.family = NFPROTO_IPV4,
#endif
	.match = ndpi_mt,
	.checkentry = ndpi_mt_check,
	.destroy = ndpi_mt_destroy,
	.matchsize = sizeof(struct xt_ndpi_mtinfo),
	.me = THIS_MODULE,
};


static int __init ndpi_mt_init(void)
{
        int ret=-ENOMEM, i;

	pr_info("xt_ndpi 2.0 (nDPI wrapper module).\n");

	/* init global detection structure */
	ndpi_struct = ndpi_init_detection_module(detection_tick_resolution,
                                                     malloc_wrapper, free_wrapper, debug_printf);

	if (ndpi_struct == NULL) {
		pr_err("xt_ndpi: global structure initialization failed.\n");
                ret = -ENOMEM;
                goto err_out;
	}

        for (i = 0; i <= NDPI_LAST_IMPLEMENTED_PROTOCOL; i++){
                atomic_set (&protocols_cnt[i], 0);

                // Set HTTP based protocols
                if ((i > 118 && i < 127) || (i > 139 && i < 146) || (i > 175 && i < 182 ) || i == 70 || i == 133) nfndpi_protocols_http[i]=1;
                else nfndpi_protocols_http[i]=0;
        }

	/* disable all protocols */
	NDPI_BITMASK_RESET(protocols_bitmask);
	ndpi_set_protocol_detection_bitmask2(ndpi_struct, &protocols_bitmask);
        
	/* allocate memory for id and flow tracking */
	size_id_struct = ndpi_detection_get_sizeof_ndpi_id_struct();
	size_flow_struct = ndpi_detection_get_sizeof_ndpi_flow_struct();


        osdpi_flow_cache = kmem_cache_create("xt_ndpi_flows",
                                             sizeof(struct osdpi_flow_node) +
                                             size_flow_struct,
                                             0, 0, NULL);

        if (!osdpi_flow_cache){
                pr_err("xt_ndpi: error creating flow cache.\n");
                ret = -ENOMEM;
                goto err_ipq;
        }
        
        osdpi_id_cache = kmem_cache_create("xt_ndpi_ids",
                                           sizeof(struct osdpi_id_node) +
                                           size_id_struct,
                                           0, 0, NULL);
        if (!osdpi_id_cache){
                pr_err("xt_ndpi: error creating ids cache.\n");
                ret = -ENOMEM;
                goto err_flow;
        }

        ret = xt_register_match(&ndpi_mt_reg);
        if (ret != 0){
                pr_err("xt_ndpi: error registering ndpi match.\n");
                ndpi_cleanup();
        }

        return ret;

err_flow:
        kmem_cache_destroy (osdpi_flow_cache);
err_ipq:
        ndpi_exit_detection_module(ndpi_struct, free_wrapper);
err_out:
        return ret;
}


static void __exit ndpi_mt_exit(void)
{
	pr_info("xt_ndpi 2.0 unload.\n");

	xt_unregister_match(&ndpi_mt_reg);
        ndpi_cleanup();
}


module_init(ndpi_mt_init);
module_exit(ndpi_mt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("G. Elian Gidoni <geg@gnu.org>");
MODULE_AUTHOR("Humberto Juca <betolj@gmail.com>");
MODULE_DESCRIPTION("nDPI wrapper");
MODULE_ALIAS("ipt_ndpi");
