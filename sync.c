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
           p->player_count <= SYNC_MAX_PLAYERS && p->mode < MODE_COUNT && p->side < SIDE_COUNT && p->state <= PLAYER_OUT;
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
    c->side = p->side;
    if (p->kill == KILL_NONE && p->station != STATION_NONE) c->safe_station = p->station;
    if (c->have_command && p->command_seq != c->command_seq) c->pending_command = (uint8_t)(p->command_zone + 1);
    c->have_command = true;
    c->command_seq = p->command_seq;
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

static void add_player(Output *out, int station, uint16_t input, int side, int state, const char *name) {
    if (out->player_count >= SYNC_MAX_PLAYERS) return;
    Player *p = &out->players[out->player_count++];
    p->used = true;
    p->station = station;
    p->input = input;
    p->side = side;
    p->state = state;
    copy_name(p->name, name);
}

static bool on_survivors_side(int mode, int side) { return mode != MODE_VERSUS || side == SIDE_SURVIVOR; }

static bool peer_here(const Sync *s, int i, uint32_t now_ms) {
    (void)s;
    return s->peers[i].used && now_ms - s->peers[i].last_ms < SYNC_PEER_TIMEOUT_MS;
}

static int count_survivors(const Sync *s, const Local *l, uint32_t now_ms, int except) {
    int n = 0;
    if (except != -1 && s->my_state == PLAYER_ALIVE && on_survivors_side(l->mode, l->side)) n++;
    for (int i = 0; i < SYNC_MAX_PEERS; i++)
        if (i != except && peer_here(s, i, now_ms) && s->peers[i].state == PLAYER_ALIVE && on_survivors_side(l->mode, s->peers[i].side)) n++;
    return n;
}

static void lose_team(Sync *s, uint32_t now_ms) {
    if (!s->team_lost) s->team_lost_ms = now_ms;
    s->team_lost = true;
}

static void set_state(Sync *s, int slot, int state) {
    if (slot < 0) s->my_state = state;
    else s->peers[slot].state = state;
}

static void player_died(Sync *s, const Local *l, int slot, int kind, int station, uint32_t now_ms) {
    int others = count_survivors(s, l, now_ms, slot);
    if (l->mode == MODE_EXTENDED && !s->revive_used && !s->rescue && others > 0) {
        s->revive_used = true;
        s->rescue = true;
        s->rescue_slot = slot;
        s->rescue_kind = kind;
        s->rescue_station = station == STATION_NONE || station == STATION_CENTER ? STATION_BED : station;
        s->rescue_deadline_ms = now_ms + SYNC_RESCUE_MS;
        s->rescue_held_ms = 0;
        set_state(s, slot, PLAYER_DOWN);
    } else if (l->last_standing) {
        set_state(s, slot, PLAYER_OUT);
        /* The last death is everyone's scare, and each copy leaves the night when it ends. */
        if (others == 0) {
            lose_team(s, now_ms);
            s->kill = kind;
        }
    } else {
        s->kill = kind;
    }
}

static uint16_t view_bit(int station) {
    switch (station) {
    case STATION_LEFT: return IN_VIEW_LEFT;
    case STATION_RIGHT: return IN_VIEW_RIGHT;
    case STATION_CLOSET: return IN_VIEW_CLOSET;
    default: return IN_VIEW_BED;
    }
}

static void step_rescue(Sync *s, const Local *l, uint32_t now_ms, uint32_t dt) {
    bool lit = s->my_state == PLAYER_ALIVE && l->station == s->rescue_station && (l->input & view_bit(s->rescue_station));
    for (int i = 0; i < SYNC_MAX_PEERS; i++) {
        const Peer *c = &s->peers[i];
        if (peer_here(s, i, now_ms) && c->state == PLAYER_ALIVE && c->station == s->rescue_station && (c->input & view_bit(s->rescue_station))) lit = true;
    }
    if (lit) s->rescue_held_ms += dt;
    if (s->rescue_held_ms >= SYNC_RESCUE_HOLD_MS) {
        s->rescue = false;
        set_state(s, s->rescue_slot, PLAYER_ALIVE);
        s->rescued_slot = s->rescue_slot;
        s->rescued_ms = now_ms;
    } else if ((int32_t)(now_ms - s->rescue_deadline_ms) >= 0) {
        s->rescue = false;
        if (l->last_standing) {
            set_state(s, s->rescue_slot, PLAYER_OUT);
            if (count_survivors(s, l, now_ms, -2) == 0) lose_team(s, now_ms);
        } else {
            s->kill = s->rescue_kind;
        }
    }
}

static void step_battery(Sync *s, const Local *l, uint32_t now_ms, uint32_t dt) {
    int lights = (l->input & (IN_VIEW_LEFT | IN_VIEW_RIGHT | IN_VIEW_CLOSET | IN_VIEW_BED)) ? 1 : 0;
    for (int i = 0; i < SYNC_MAX_PEERS; i++)
        if (peer_here(s, i, now_ms) && (s->peers[i].input & (IN_VIEW_LEFT | IN_VIEW_RIGHT | IN_VIEW_CLOSET | IN_VIEW_BED))) lights++;
    int rate = lights ? -lights * SYNC_BATTERY_DRAIN : SYNC_BATTERY_CHARGE;
    s->charge_left += rate * (int)dt;
    s->battery += s->charge_left / 1000;
    s->charge_left %= 1000;
    if (s->battery < 0) s->battery = 0;
    if (s->battery > SYNC_BATTERY_FULL) s->battery = SYNC_BATTERY_FULL;
}

static void step_host(Sync *s, uint32_t now_ms, const Local *l, Output *out, Packet *send) {
    uint32_t dt = s->last_step_ms ? now_ms - s->last_step_ms : 0;
    s->last_step_ms = now_ms;
    if (dt > 1000) dt = 1000;
    if (l->in_level && !s->was_in_level) {
        s->epoch++;
        s->kill = KILL_NONE;
        s->my_state = PLAYER_ALIVE;
        s->revive_used = false;
        s->rescue = false;
        s->team_lost = false;
        s->battery = SYNC_BATTERY_FULL;
        s->charge_left = 0;
        s->rescued_ms = 0;
        for (int i = 0; i < SYNC_MAX_PEERS; i++) {
            s->peers[i].kill = KILL_NONE;
            s->peers[i].state = PLAYER_ALIVE;
        }
    }
    s->was_in_level = l->in_level;

    fill(s, send, l);
    send->mode = (uint8_t)l->mode;
    send->last_standing = l->last_standing;
    if (l->kill == KILL_NONE && l->station != STATION_NONE) s->my_safe_station = l->station;
    bool in_grace = s->rescued_ms && now_ms - s->rescued_ms < SYNC_RESCUE_GRACE_MS;
    if (l->in_level && l->kill != KILL_NONE && s->kill == KILL_NONE && s->my_state == PLAYER_ALIVE && on_survivors_side(l->mode, l->side) && !(in_grace && s->rescued_slot == -1))
        player_died(s, l, -1, l->kill, s->my_safe_station, now_ms);
    for (int i = 0; i < SYNC_MAX_PEERS; i++) {
        Peer *c = &s->peers[i];
        if (!peer_here(s, i, now_ms)) continue;
        /* A late report from a death that is already handled must not count as a new one. */
        bool counts = c->state == PLAYER_ALIVE && !(in_grace && s->rescued_slot == i) && on_survivors_side(l->mode, c->side);
        if (c->kill != KILL_NONE && !counts) c->kill = KILL_NONE;
        if (l->in_level && c->kill != KILL_NONE && c->kill_epoch == s->epoch && s->kill == KILL_NONE) {
            int kind = c->kill;
            c->kill = KILL_NONE;
            player_died(s, l, i, kind, c->safe_station, now_ms);
        }
    }
    /* Someone who leaves can't be left standing in for the team. */
    if (l->in_level && l->last_standing && !s->rescue && s->kill == KILL_NONE && count_survivors(s, l, now_ms, -2) == 0 && l->value_count > 0) lose_team(s, now_ms);
    if (l->in_level && s->rescue) step_rescue(s, l, now_ms, dt);
    if (l->in_level && l->mode == MODE_EXTENDED) step_battery(s, l, now_ms, dt);

    for (int i = 0; i < SYNC_MAX_PEERS; i++) {
        Peer *c = &s->peers[i];
        if (!peer_here(s, i, now_ms)) continue;
        out->others |= c->input;
        add_player(out, c->station, c->input, c->side, c->state, c->name);
        for (int e = 0; e < EV_COUNT; e++) {
            if (c->pending_events[e] && l->in_level) out->events[e] = true;
            c->pending_events[e] = false;
        }
        if (c->pending_command) {
            out->commands[out->command_count].side = c->side;
            out->commands[out->command_count].zone = c->pending_command - 1;
            out->command_count++;
            c->pending_command = 0;
        }
        if (send->player_count < SYNC_MAX_PLAYERS) {
            int n = send->player_count++;
            send->players[n].station = (uint8_t)c->station;
            send->players[n].side = (uint8_t)c->side;
            send->players[n].state = (uint8_t)c->state;
            send->players[n].input = c->input;
            copy_name(send->players[n].name, c->name);
        }
    }
    if (l->in_level && s->kill != KILL_NONE && l->kill == KILL_NONE) out->apply_kill = s->kill;
    out->mode = l->mode;
    out->last_standing = l->last_standing;
    out->my_state = s->my_state;
    out->battery = s->battery;
    out->team_lost = s->team_lost;
    out->end_night = s->team_lost && (s->kill == KILL_NONE || now_ms - s->team_lost_ms >= SYNC_FOLLOW_KILL_GRACE_MS);
    out->survivors = count_survivors(s, l, now_ms, -2);
    out->rescue = s->rescue;
    out->rescue_station = s->rescue_station;
    out->rescue_left_ms = s->rescue ? (int)(s->rescue_deadline_ms - now_ms) : 0;
    const char *down_name = s->rescue_slot < 0 ? s->name : s->peers[s->rescue_slot].name;
    if (s->rescue) copy_name(out->rescue_name, down_name);
    send->epoch = s->epoch;
    send->kill = (uint8_t)s->kill;
    send->state = (uint8_t)s->my_state;
    send->side = (uint8_t)l->side;
    send->battery = (uint16_t)s->battery;
    send->rescue = s->rescue;
    send->rescue_station = (uint8_t)s->rescue_station;
    send->rescue_left_ms = (uint16_t)out->rescue_left_ms;
    copy_name(send->rescue_name, out->rescue_name);
    send->value_count = (uint8_t)l->value_count;
    memcpy(send->values, l->values, sizeof send->values);
}

static void step_client(Sync *s, uint32_t now_ms, const Local *l, Output *out, Packet *send) {
    const Packet *h = &s->host;
    bool fresh = s->have_host && now_ms - s->host_ms < SYNC_PEER_TIMEOUT_MS;
    out->my_state = s->my_state; /* holds through a load, while no packets arrive */
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
            uint32_t grace = h->kill != KILL_NONE || l->kill != KILL_NONE || l->scaring ? SYNC_FOLLOW_KILL_GRACE_MS : SYNC_FOLLOW_GRACE_MS;
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
        out->mode = h->mode;
        out->last_standing = h->last_standing;
        out->battery = h->battery;
        out->rescue = h->rescue;
        out->rescue_station = h->rescue_station;
        out->rescue_left_ms = h->rescue_left_ms;
        copy_name(out->rescue_name, h->rescue_name);
        add_player(out, h->station, h->input, h->side, h->state, h->name);
        if (h->state == PLAYER_ALIVE && on_survivors_side(h->mode, h->side)) out->survivors++;
        for (int i = 0; i < h->player_count; i++) {
            bool me = strncmp(h->players[i].name, s->name, SYNC_NAME_LEN) == 0;
            if (h->players[i].state == PLAYER_ALIVE && on_survivors_side(h->mode, h->players[i].side)) out->survivors++;
            if (me) {
                s->my_state = h->players[i].state;
                out->my_state = s->my_state;
                continue;
            }
            add_player(out, h->players[i].station, h->players[i].input, h->players[i].side, h->players[i].state, h->players[i].name);
            out->others |= h->players[i].input;
        }
        if (l->in_level && s->my_epoch != 0 && h->epoch == s->my_epoch && h->kill != KILL_NONE && l->kill == KILL_NONE) out->apply_kill = h->kill;
    }

    for (int i = 0; i < EV_COUNT; i++)
        if (l->events[i]) s->event_counts[i]++;
    fill(s, send, l);
    send->epoch = s->my_epoch;
    send->kill = (uint8_t)l->kill;
    send->side = (uint8_t)l->side;
    send->command_seq = l->command_seq;
    send->command_zone = l->command_zone;
    memcpy(send->event_counts, s->event_counts, sizeof send->event_counts);
}

void sync_step(Sync *s, uint32_t now_ms, const Local *l, Output *out, Packet *send) {
    memset(out, 0, sizeof *out);
    s->seq++;
    if (s->is_host) step_host(s, now_ms, l, out, send);
    else step_client(s, now_ms, l, out, send);
}
