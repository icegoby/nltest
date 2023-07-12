#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <linux/nl80211.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <linux/version.h>
#include "ieee80211.h"

#define IFNAME "wlan1"
#define FREQ 5220

#define CONTENT_LEN    100
#define ACTION_CATEGORY 40

static uint8_t dst_addr[ETH_ALEN] = {0x01, 0x00, 0x5e, 0x40, 0x01, 0x02};

struct nl80211_state {
    struct nl_sock *sock;
    int id;
};

static int
get_macaddr(char *ifname, uint8_t *macaddr)
{
    int s;
    struct ifreq ifr;

    s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) {
        fprintf(stderr, "%s[%d]: Failed to open socket\n", __func__, __LINE__);
        return -1;
    }
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(s, SIOCGIFHWADDR, &ifr) < 0) {
        fprintf(stderr, "%s[%d]: Failed to ioctl\n", __func__, __LINE__);
        close(s);
        return -1;
    }
    close(s);
    memcpy(macaddr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    return 0;
}

static struct nl_msg *
make_frame(struct nl_msg *msg, uint8_t *buf, int buf_len)
{
    struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)buf;
    uint8_t *head = &mgmt->u.action.u.generic.content;
    uint8_t myaddr[ETH_ALEN];
    int len = head - buf + CONTENT_LEN;
    uint8_t *p = head;
    int i;

    if (buf_len < len) {
        fprintf(stderr, "%s[%d]: buf is too small.\n", __func__, __LINE__);
        return NULL;
    }
    if (get_macaddr(IFNAME, myaddr) < 0) {
        fprintf(stderr, "%s[%d]: Failed to get MAC addr\n", __func__, __LINE__);
        return NULL;
    }
    memset(buf, 0, buf_len);
    mgmt->frame_control = IEEE80211_FTYPE_MGMT | IEEE80211_STYPE_ACTION;
    memcpy(mgmt->da, dst_addr, ETH_ALEN);
    memcpy(mgmt->sa, myaddr, ETH_ALEN);
    memcpy(mgmt->bssid, myaddr, ETH_ALEN);
    mgmt->u.action.category = ACTION_CATEGORY;
    mgmt->u.action.u.generic.action_code = 0;
    for (i = 0; i < CONTENT_LEN; i++) {
        *p = i % 0x100;
        ++p;
    }
    if (nla_put(msg, NL80211_ATTR_FRAME, len, buf)) {
        fprintf(stderr, "%s[%d]: Failed to put frame\n", __func__, __LINE__);
        return NULL;
    }
    return msg;
}

static int
error_handler(struct sockaddr_nl *nla, struct nlmsgerr *err, void *arg)
{
    struct nlmsghdr *nlh = (struct nlmsghdr *)err - 1;
    int len = nlh->nlmsg_len;
    struct nlattr *attrs;
    struct nlattr *tb[NLMSGERR_ATTR_MAX + 1];
    int *ret = arg;
    int ack_len = sizeof(*nlh) + sizeof(int) + sizeof(*nlh);

    *ret = err->error;

    fprintf(stderr, "%s[%d]: called\n", __func__, __LINE__);
    if (!(nlh->nlmsg_flags & NLM_F_ACK_TLVS))
        return NL_STOP;

    if (!(nlh->nlmsg_flags & NLM_F_CAPPED))
        ack_len += err->msg.nlmsg_len - sizeof(*nlh);

    if (len <= ack_len)
        return NL_STOP;

    attrs = (void *)((unsigned char *)nlh + ack_len);
    len -= ack_len;

    nla_parse(tb, NLMSGERR_ATTR_MAX, attrs, len, NULL);
    if (tb[NLMSGERR_ATTR_MSG]) {
        len = strnlen((char *)nla_data(tb[NLMSGERR_ATTR_MSG]), nla_len(tb[NLMSGERR_ATTR_MSG]));
        fprintf(stderr, "%s[%d]: kernel reports: %*s\n", __func__, __LINE__, len,
                (char *)nla_data(tb[NLMSGERR_ATTR_MSG]));
    }

    return NL_STOP;
}

static int
finish_handler(struct nl_msg *msg, void *arg)
{
    int *ret = arg;
    *ret = 0;
    fprintf(stderr, "%s[%d]: called\n", __func__, __LINE__);
    return NL_SKIP;
}

static int
ack_handler(struct nl_msg *msg, void *arg)
{
    int *ret = arg;
    *ret = 0;
    fprintf(stderr, "%s[%d]: called\n", __func__, __LINE__);
    return NL_STOP;
}

static int (*registered_handler)(struct nl_msg *, void *);
static void *registered_handler_data;

void register_handler(int (*handler)(struct nl_msg *, void *), void *data)
{
    registered_handler = handler;
    registered_handler_data = data;
    fprintf(stderr, "%s[%d]: called\n", __func__, __LINE__);
}

int
valid_handler(struct nl_msg *msg, void *arg)
{
    fprintf(stderr, "%s[%d]: called\n", __func__, __LINE__);
    if (registered_handler)
        return registered_handler(msg, registered_handler_data);

    return NL_OK;
}

static int
nl80211_init(struct nl80211_state *state)
{
    int err;

    state->sock = nl_socket_alloc();
    if (state->sock == NULL) {
        fprintf(stderr, "%s[%d]: Failed to alloc netlink socket\n", __func__, __LINE__);
        return -ENOMEM;
    }
    if (genl_connect(state->sock)) {
        fprintf(stderr, "%s[%d]: Failed to connect generic netlink\n", __func__, __LINE__);
        err = -ENOLINK;
        goto out;
    }
    nl_socket_set_buffer_size(state->sock, 8192, 8192);

    err = 1;
    setsockopt(nl_socket_get_fd(state->sock), SOL_NETLINK, NETLINK_EXT_ACK, &err, sizeof(err));
    state->id = genl_ctrl_resolve(state->sock, "nl80211");
    if (state->id < 0) {
        fprintf(stderr, "%s[%d]: nl80211 not found\n", __func__, __LINE__);
        err = -ENOENT;
        goto out;
    }

    return 0;

out:
    nl_socket_free(state->sock);
    return err;
}

static void
nl80211_deinit(struct nl80211_state *state)
{
    nl_socket_free(state->sock);
}

#if 0
static void
usage(void)
{
    fprintf(stderr, "Usage: nltest iftype\n\tiftype: ap, sta\n\n");
}
#endif

int
main(int argc, char **argv)
{
    struct nl80211_state state;
    int err = 0;
    int if_index;
    struct nl_msg *msg = NULL;
    struct nl_cb *cb = NULL;
    struct nl_cb *s_cb = NULL;
    uint8_t buf[2048];

#if 0
    enum nl80211_iftype iftype = NL80211_IFTYPE_UNSPECIFIED;
    if (argc != 2) {
        usage();
        return 0;
    }

    if (!strcmp(argv[1], "ap"))
        iftype = NL80211_IFTYPE_AP;
    else if (!strcmp(argv[1], "sta"))
        iftype = NL80211_IFTYPE_STATION;
    else {
        usage();
        return 1;
    }
    fprintf(stderr, "%s[%d]: argv[1] = '%s', iftype = %d\n", __func__, __LINE__, argv[1], iftype);
#endif
    memset(&state, 0, sizeof(state));
    if ((err = nl80211_init(&state)) != 0) {
        fprintf(stderr, "%s[%d]: Failed to init nl80211 socket (%d)\n", __func__, __LINE__, err);
        goto out;
    }

    fprintf(stderr, "%s[%d]: id = %d\n", __func__, __LINE__, state.id);

    if_index = if_nametoindex(IFNAME);
    fprintf(stderr, "%s[%d]: if_index = %d\n", __func__, __LINE__, if_index);
    if (if_index == 0) {
        fprintf(stderr, "%s[%d]: Interface '%s' not found\n", __func__, __LINE__, IFNAME);
        err = 1;
        goto out;
    }

    msg = nlmsg_alloc();
    if (msg == NULL) {
        fprintf(stderr, "%s[%d]: Failed to alloc netlink message\n", __func__, __LINE__);
        err = 2;
        goto out;
    }

    cb = nl_cb_alloc(NL_CB_DEBUG);
    s_cb = nl_cb_alloc(NL_CB_DEBUG);
    if (cb == NULL || s_cb == NULL) {
        fprintf(stderr, "%s[%d]: Failed to alloc netlink callbacks\n", __func__, __LINE__);
        err = 3;
        goto out;
    }

    genlmsg_put(msg, 0, 0, state.id, 0, 0, NL80211_CMD_FRAME, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, if_index);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)
    NLA_PUT_U32(msg, NL80211_ATTR_WIPHY_FREQ, FREQ);
#endif
    if (make_frame(msg, buf, 2048) == NULL) {
        fprintf(stderr, "%s[%d]: Failed to make frame\n", __func__, __LINE__);
        err = 8;
        goto out;
    }

#if 0
    genlmsg_put(msg, 0, 0, state.id, 0, 0, NL80211_CMD_SET_INTERFACE, 0);
    NLA_PUT_U32(msg, NL80211_ATTR_IFINDEX, if_index);
    NLA_PUT_U32(msg, NL80211_ATTR_IFTYPE, iftype);
#endif
    nl_socket_set_cb(state.sock, s_cb);
    err = nl_send_auto_complete(state.sock, msg);
    if (err < 0) {
        fprintf(stderr, "%s[%d]: err = %d\n", __func__, __LINE__, err);
        goto out;
    }

    err = 1;

    nl_cb_err(cb, NL_CB_CUSTOM, error_handler, &err);
    nl_cb_set(cb, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &err);
    nl_cb_set(cb, NL_CB_ACK, NL_CB_CUSTOM, ack_handler, &err);
    nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, valid_handler, NULL);

    while (err > 0)
        nl_recvmsgs(state.sock, cb);

out:
    nl_cb_put(cb);
    nl_cb_put(s_cb);
    nlmsg_free(msg);
    nl80211_deinit(&state);
    return err;

nla_put_failure:
    fprintf(stderr, "%s[%d]: Failed to build message\n", __func__, __LINE__);
    return 4;
}
