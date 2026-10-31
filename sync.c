#include "sync.h"
#include <string.h>

static void copy_name(char *to, const char *from) {
    strncpy(to, from, SYNC_NAME_LEN - 1);
    to[SYNC_NAME_LEN - 1] = 0;
}

void sync_init(Sync *s, bool is_host, uint32_t session, const char *name) {
    memset(s, 0, sizeof *s);
    s->is_host = is_host;
    s->session = session;
    copy_name(s->name, name);
}

static bool newer(uint32_t seq, uint32_t last) { return (int32_t)(seq - last) > 0; }

static bool valid(const Packet *p) {
    return p->magic == SYNC_MAGIC && p->version == SYNC_VERSION && p->kill < KILL_COUNT && p->value_count <= SYNC_VALUES &&
           p->player_count <= SYNC_MAX_PLAYERS;
}

static bool recv_on_host(Sync *s, int peer, const Packet *p, uint32_t now_ms) {
    if (p->is_host || peer < 0 || peer >= SYNC_MAX_PEERS) return false;
    Peer *c = &s->peers[peer];
    if (c->used && c->session == p->session && !newer(p->seq, c->last_seq)) return false;
    if (p->closing) {
        c->used = false;
        return true;
    }
    if (!c->used || c->session != p->session) memset(c, 0, sizeof *c);
    c->used = true;
    c->session = p->session;
    c->last_seq = p->seq;
    c->last_ms = now_ms;
    c->station = p->station;
    c->input = p->input;
    copy_name(c->name, p->name);
    if (p->kill != KILL_NONE) {
        c->kill = p->kill;
        c->kill_epoch = p->epoch;
    }
    /* Counters survive packet loss: a jump of two means two events, each is applied once. */
    for (int i = 0; i < EV_COUNT; i++) {
        if (c->have_events && p->event_counts[i] != c->event_counts[i]) c->pending_events[i] = true;
        c->event_counts[i] = p->event_counts[i];
    }
    c->have_events = true;
    return true;
}

bool sync_recv(Sync *s, int peer, const Packet *p, uint32_t now_ms) {
    if (!valid(p)) return false;
    if (s->is_host) return recv_on_host(s, peer, p, now_ms);
    if (!p->is_host) return false;
    if (s->have_host && s->host_session == p->session && !newer(p->seq, s->host_seq)) return false;
    s->have_host = true;
    s->host_session = p->session;
    s->host_seq = p->seq;
    s->host_ms = now_ms;
    s->host = *p;
    if (p->closing) s->host_closed = true;
    return true;
}

static void fill(const Sync *s, Packet *p, const Local *l) {
    memset(p, 0, sizeof *p);
    p->magic = SYNC_MAGIC;
    p->version = SYNC_VERSION;
    p->is_host = s->is_host;
    p->station = (uint8_t)l->station;
    p->session = s->session;
    p->seq = s->seq;
    p->frame = l->frame;
    p->input = l->input;
    p->in_level = l->in_level;
    copy_name(p->name, s->name);
}

static void add_player(Output *out, int station, uint16_t input, const char *name) {
    if (out->player_count >= SYNC_MAX_PLAYERS) return;
    Player *p = &out->players[out->player_count++];
    p->used = true;
    p->station = station;
    p->input = input;
    copy_name(p->name, name);
}

static void step_host(Sync *s, uint32_t now_ms, const Local *l, Output *out, Packet *send) {
    if (l->in_level && !s->was_in_level) {
        s->epoch++;
        s->kill = KILL_NONE;
        for (int i = 0; i < SYNC_MAX_PEERS; i++) s->peers[i].kill = KILL_NONE;
    }
    s->was_in_level = l->in_level;

    fill(s, send, l);
    if (l->in_level && l->kill != KILL_NONE && s->kill == KILL_NONE) s->kill = l->kill;
    for (int i = 0; i < SYNC_MAX_PEERS; i++) {
        Peer *c = &s->peers[i];
        if (!c->used || now_ms - c->last_ms >= SYNC_PEER_TIMEOUT_MS) continue;
        if (l->in_level && c->kill != KILL_NONE && c->kill_epoch == s->epoch && s->kill == KILL_NONE) s->kill = c->kill;
        out->others |= c->input;
        add_player(out, c->station, c->input, c->name);
        for (int e = 0; e < EV_COUNT; e++) {
            if (c->pending_events[e] && l->in_level) out->events[e] = true;
            c->pending_events[e] = false;
        }
        if (send->player_count < SYNC_MAX_PLAYERS) {
            int n = send->player_count++;
            send->players[n].station = (uint8_t)c->station;
            send->players[n].input = c->input;
            copy_name(send->players[n].name, c->name);
        }
    }
    if (l->in_level && s->kill != KILL_NONE && l->kill == KILL_NONE) out->apply_kill = s->kill;
    send->epoch = s->epoch;
    send->kill = (uint8_t)s->kill;
    send->value_count = (uint8_t)l->value_count;
    memcpy(send->values, l->values, sizeof send->values);
}

static void step_client(Sync *s, uint32_t now_ms, const Local *l, Output *out, Packet *send) {
    const Packet *h = &s->host;
    bool fresh = s->have_host && now_ms - s->host_ms < SYNC_PEER_TIMEOUT_MS;
    out->host_lost = s->host_closed || (s->have_host && now_ms - s->host_ms >= SYNC_HOST_LOST_MS);

    if (!l->in_level) {
        s->my_epoch = 0;
        s->kill_applied = false;
    }
    if (fresh) {
        if (l->in_level && h->in_level && h->frame == l->frame && s->my_epoch != h->epoch) {
            s->my_epoch = h->epoch;
            s->kill_applied = false;
        }
        if (h->frame >= 0 && h->frame != l->frame) {
            if (!s->mismatch) {
                s->mismatch = true;
                s->mismatch_ms = now_ms;
            }
            uint32_t grace = h->kill != KILL_NONE || l->kill != KILL_NONE ? SYNC_FOLLOW_KILL_GRACE_MS : SYNC_FOLLOW_GRACE_MS;
            if (now_ms - s->mismatch_ms >= grace && (!s->sent_goto || now_ms - s->last_goto_ms >= SYNC_FOLLOW_RETRY_MS)) {
                out->go_to_frame = true;
                out->frame = h->frame;
                s->sent_goto = true;
                s->last_goto_ms = now_ms;
            }
        } else {
            s->mismatch = false;
            s->sent_goto = false;
            if (h->value_count == l->value_count) {
                out->apply_values = true;
                memcpy(out->values, h->values, sizeof out->values);
            }
        }
        out->others = h->input;
        add_player(out, h->station, h->input, h->name);
        for (int i = 0; i < h->player_count; i++) {
            if (strncmp(h->players[i].name, s->name, SYNC_NAME_LEN) == 0) continue;
            add_player(out, h->players[i].station, h->players[i].input, h->players[i].name);
            out->others |= h->players[i].input;
        }
        if (l->in_level && s->my_epoch != 0 && h->epoch == s->my_epoch && h->kill != KILL_NONE && l->kill == KILL_NONE) out->apply_kill = h->kill;
    }

    for (int i = 0; i < EV_COUNT; i++)
        if (l->events[i]) s->event_counts[i]++;
    fill(s, send, l);
    send->epoch = s->my_epoch;
    send->kill = (uint8_t)l->kill;
    memcpy(send->event_counts, s->event_counts, sizeof send->event_counts);
}

void sync_step(Sync *s, uint32_t now_ms, const Local *l, Output *out, Packet *send) {
    memset(out, 0, sizeof *out);
    s->seq++;
    if (s->is_host) step_host(s, now_ms, l, out, send);
    else step_client(s, now_ms, l, out, send);
}
