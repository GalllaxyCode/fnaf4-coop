#include "sync.h"
#include <string.h>

void sync_init(Sync *s, bool is_host, uint32_t session) {
    memset(s, 0, sizeof *s);
    s->is_host = is_host;
    s->session = session;
}

static bool newer(uint32_t seq, uint32_t last) { return (int32_t)(seq - last) > 0; }

bool sync_recv(Sync *s, int peer, const Packet *p, uint32_t now_ms) {
    if (p->magic != SYNC_MAGIC || p->version != SYNC_VERSION || p->result > RESULT_ABORTED) return false;
    if (s->is_host) {
        if (p->is_host || peer < 0 || peer >= SYNC_MAX_PEERS) return false;
        Peer *c = &s->peers[peer];
        if (c->used && c->session == p->session && !newer(p->seq, c->last_seq)) return false;
        if (!c->used || c->session != p->session) memset(c, 0, sizeof *c);
        c->used = true;
        c->session = p->session;
        c->last_seq = p->seq;
        c->last_ms = now_ms;
        if (p->dead) {
            c->dead = true;
            c->dead_epoch = p->epoch;
        }
        return true;
    }
    if (!p->is_host) return false;
    if (s->have_host && s->host_session == p->session && !newer(p->seq, s->host_seq)) return false;
    s->have_host = true;
    s->host_session = p->session;
    s->host_seq = p->seq;
    s->host_ms = now_ms;
    s->host = *p;
    return true;
}

static void fill(const Sync *s, Packet *p, const World *w) {
    memset(p, 0, sizeof *p);
    p->magic = SYNC_MAGIC;
    p->version = SYNC_VERSION;
    p->is_host = s->is_host;
    p->in_level = w->in_level;
    p->night = (uint8_t)w->night;
    p->session = s->session;
    p->seq = s->seq;
    p->hour = (uint8_t)w->hour;
}

static void step_host(Sync *s, uint32_t now_ms, const World *w, Writes *out, Packet *send) {
    if (w->in_level && !s->was_in_level) {
        s->epoch++;
        s->result = RESULT_NONE;
    }
    if (w->in_level) {
        s->last_hour = w->hour;
        s->last_night = w->night;
        if (w->gameover) s->result = RESULT_DEAD;
        for (int i = 0; i < SYNC_MAX_PEERS; i++) {
            const Peer *c = &s->peers[i];
            if (c->used && c->dead && c->dead_epoch == s->epoch && now_ms - c->last_ms < SYNC_PEER_TIMEOUT_MS)
                s->result = RESULT_DEAD;
        }
        if (s->result == RESULT_DEAD && !w->gameover) out->force_death = true;
    } else if (s->was_in_level && s->result == RESULT_NONE) {
        s->exit_pending = true;
        s->exit_ms = now_ms;
    }
    if (s->exit_pending && !w->in_level) {
        /* The jump can beat any tick that would see hour 6, so the win frame itself decides. */
        if (w->won_screen) s->result = RESULT_WON;
        else if (now_ms - s->exit_ms >= SYNC_EXIT_WAIT_MS) s->result = RESULT_ABORTED;
        if (s->result != RESULT_NONE) s->exit_pending = false;
    } else {
        s->exit_pending = false;
    }
    s->was_in_level = w->in_level;
    fill(s, send, w);
    send->epoch = s->epoch;
    send->result = (uint8_t)s->result;
    if (!w->in_level) {
        send->hour = (uint8_t)s->last_hour;
        send->night = (uint8_t)s->last_night;
    }
}

static void step_client(Sync *s, uint32_t now_ms, const World *w, Writes *out, Packet *send) {
    const Packet *h = &s->host;
    bool fresh = s->have_host && now_ms - s->host_ms < SYNC_PEER_TIMEOUT_MS;
    s->night_mismatch = false;
    /* A new level starts unattached, so a result left over from the previous night can't apply. */
    if (w->in_level && !s->was_in_level) {
        s->my_epoch = 0;
        s->my_dead = false;
    }
    s->was_in_level = w->in_level;
    if (fresh && w->in_level) {
        if (h->in_level && h->epoch != s->my_epoch && h->result == RESULT_NONE) {
            s->my_epoch = h->epoch;
            s->my_dead = false;
        }
        if (w->gameover && h->epoch == s->my_epoch && s->my_epoch != 0) s->my_dead = true;
        if (h->epoch == s->my_epoch && s->my_epoch != 0) {
            if (h->result == RESULT_DEAD) {
                if (!w->gameover) out->force_death = true;
            } else if (h->result == RESULT_WON) {
                if (!w->gameover && w->hour != SYNC_WIN_HOUR) {
                    out->set_hour = true;
                    out->hour = SYNC_WIN_HOUR;
                }
            } else if (h->in_level && h->result == RESULT_NONE) {
                if (h->night != w->night) s->night_mismatch = true;
                else if (!w->gameover && h->hour != w->hour) {
                    out->set_hour = true;
                    out->hour = h->hour;
                }
            }
        }
    }
    fill(s, send, w);
    send->epoch = s->my_epoch;
    send->dead = s->my_dead;
}

void sync_step(Sync *s, uint32_t now_ms, const World *w, Writes *out, Packet *send) {
    memset(out, 0, sizeof *out);
    s->seq++;
    if (s->is_host) step_host(s, now_ms, w, out, send);
    else step_client(s, now_ms, w, out, send);
}
