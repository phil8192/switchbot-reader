// console_simple_ncurses.c
// Simple ncurses dashboard for SwitchBot JSON stream (no graphs)
// - Excludes "attic"
// - Shows averages for Inside (location != "garden") and Garden
// - Per-row dew point (requires temp °C + RH %)
// - Table sorted by Device ID
// - Indoor humidity <30% or >60% highlighted with RED background
// Build: gcc -O2 -Wall -Wextra -o console_simple_ncurses console_simple_ncurses.c -lncursesw -ljansson -lpthread -lm
// Run:   SB_STALE_SECS=900 python -u switchbot.py -a -o json | ./console_simple_ncurses

#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#include <ncurses.h>
#include <wchar.h>
#include <locale.h>
#include <jansson.h>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <math.h>

#define MAX_DEVICES     1024
#define MAX_ID_LEN      96
#define MAX_LOC_LEN     96
#define MAX_LINE_LEN    8192

#define FRAME_SLEEP_MS  25

#define TABLE_MIN_H     8
#define PANEL_MIN_H     3
#define PANEL_MIN_W     24

#define CP_WARN 1  // white on red for humidity alert

// -------- utility ----------
static inline long long now_i(void) { return (long long)time(NULL); }
static inline int imin(int a, int b){ return a < b ? a : b; }
static inline int imax(int a, int b){ return a > b ? a : b; }
static inline int is_finite(double x){ return !(x!=x) && x>-1e300 && x<1e300; }

static void sleep_ms(int ms){
    struct timespec ts;
    ts.tv_sec  = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
static void trim_inplace(char *s){
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    size_t len = strlen(p);
    memmove(s, p, len+1);
    while (len && isspace((unsigned char)s[len-1])) { s[len-1] = 0; len--; }
}
static int equals_ci(const char *a, const char *b){
    while (*a && *b){
        char ca=(char)tolower((unsigned char)*a++);
        char cb=(char)tolower((unsigned char)*b++);
        if (ca!=cb) return 0;
    }
    return *a==0 && *b==0;
}

// -------- dew point (Magnus-Tetens, Celsius) ----------
static double dewpoint_c(double t_c, double rh_pct){
    if (!is_finite(t_c) || !is_finite(rh_pct) || rh_pct<=0.0 || rh_pct>100.0) return NAN;
    const double a = 17.62;
    const double b = 243.12; // °C
    double gamma = log(rh_pct/100.0) + (a * t_c) / (b + t_c);
    return (b * gamma) / (a - gamma);
}

// -------- device model ----------
typedef struct {
    char id[MAX_ID_LEN];
    char location[MAX_LOC_LEN];
    long long ts;
    double temp; int has_temp;
    double rh;   int has_rh;
} Device;

static Device devices[MAX_DEVICES];
static int device_count = 0;

// freshness window for averages (seconds), configurable via env
static int g_stale_secs = 900;
static int g_has_colors = 0;

// -------- queue (reader -> UI) ----------
typedef struct {
    char **buf;
    int cap;
    int head;
    int tail;
    pthread_mutex_t mtx;
    pthread_cond_t  cv;
} LineQueue;

static LineQueue* q_create(int cap){
    LineQueue *q = (LineQueue*)calloc(1, sizeof(LineQueue));
    q->buf = (char**)calloc(cap, sizeof(char*));
    q->cap = cap;
    pthread_mutex_init(&q->mtx, NULL);
    pthread_cond_init(&q->cv, NULL);
    return q;
}
static void q_destroy(LineQueue *q){
    if (!q) return;
    for (int i=0;i<q->cap;i++) free(q->buf[i]);
    free(q->buf);
    pthread_mutex_destroy(&q->mtx);
    pthread_cond_destroy(&q->cv);
    free(q);
}
static void q_push(LineQueue *q, const char *line){
    pthread_mutex_lock(&q->mtx);
    int next = (q->tail + 1) % q->cap;
    if (next == q->head){
        free(q->buf[q->head]); q->buf[q->head]=NULL;
        q->head = (q->head + 1) % q->cap;
    }
    q->buf[q->tail] = strdup(line);
    q->tail = next;
    pthread_cond_signal(&q->cv);
    pthread_mutex_unlock(&q->mtx);
}
static char* q_pop_nowait(LineQueue *q){
    pthread_mutex_lock(&q->mtx);
    if (q->head == q->tail){
        pthread_mutex_unlock(&q->mtx);
        return NULL;
    }
    char *s = q->buf[q->head]; q->buf[q->head]=NULL;
    q->head = (q->head + 1) % q->cap;
    pthread_mutex_unlock(&q->mtx);
    return s;
}

// -------- reader thread ----------
typedef struct {
    LineQueue *q;
    int stop;
    pthread_t thr;
} Reader;

static void* reader_main(void *arg){
    Reader *r = (Reader*)arg;
    char line[MAX_LINE_LEN];
    while (!r->stop){
        if (!fgets(line, sizeof(line), stdin)){
            sleep_ms(30);
            continue;
        }
        // push raw line; upstream expected to be one-JSON-per-line
        q_push(r->q, line);
    }
    return NULL;
}
static void reader_start(Reader *r, LineQueue *q){ r->q=q; r->stop=0; pthread_create(&r->thr, NULL, reader_main, r); }
static void reader_stop(Reader *r){ r->stop=1; pthread_join(r->thr, NULL); }

// -------- upsert ----------
static int find_device(const char *id, const char *location){
    for (int i=0;i<device_count;i++){
        if (strcmp(devices[i].id, id)==0 && strcmp(devices[i].location, location)==0) return i;
    }
    return -1;
}
static int add_device(const char *id, const char *location){
    if (device_count >= MAX_DEVICES) return -1;
    snprintf(devices[device_count].id,       sizeof(devices[device_count].id),       "%s", id?id:"");
    snprintf(devices[device_count].location, sizeof(devices[device_count].location), "%s", location?location:"");
    devices[device_count].ts=0;
    devices[device_count].temp=0; devices[device_count].has_temp=0;
    devices[device_count].rh=0;   devices[device_count].has_rh=0;
    return device_count++;
}

static int excluded_location(const char *loc){
    return equals_ci(loc, "attic");
}

// parse one JSON line (object) and update devices
static void process_json_line(const char *line){
    json_error_t err;
    json_t *root = json_loads(line, 0, &err);
    if (!root || !json_is_object(root)){ if (root) json_decref(root); return; }

    // id
    const char *id = NULL;
    json_t *jid = json_object_get(root, "id");
    if (json_is_string(jid)) id = json_string_value(jid);
    if (!id){
        json_t *jdid = json_object_get(root, "device_id");
        if (json_is_string(jdid)) id = json_string_value(jdid);
    }
    if (!id){ json_decref(root); return; }

    // location
    const char *location = NULL;
    json_t *jloc = json_object_get(root, "location");
    if (json_is_string(jloc)) location = json_string_value(jloc);
    if (!location){
        json_t *jroom = json_object_get(root, "room");
        if (json_is_string(jroom)) location = json_string_value(jroom);
    }
    if (!location) location = "unknown";

    // timestamp (try several fields)
    long long ts = 0;
    json_t *jts = json_object_get(root, "ts");
    if (json_is_integer(jts)) ts = (long long)json_integer_value(jts);
    else if (json_is_real(jts)) ts = (long long)json_number_value(jts);
    if (!ts){
        json_t *jtime = json_object_get(root, "time");
        if (json_is_integer(jtime)) ts = (long long)json_integer_value(jtime);
        else if (json_is_real(jtime)) ts = (long long)json_number_value(jtime);
    }
    if (!ts) ts = now_i();

    // values
    double temp = NAN; int has_temp=0;
    json_t *jt = json_object_get(root, "temp");
    if (json_is_number(jt)){ temp = json_number_value(jt); has_temp=1; }
    if (!has_temp){
        jt = json_object_get(root, "temperature");
        if (json_is_number(jt)){ temp = json_number_value(jt); has_temp=1; }
    }
    if (!has_temp){
        jt = json_object_get(root, "temperature_c");
        if (json_is_number(jt)){ temp = json_number_value(jt); has_temp=1; }
    }

    double rh = NAN; int has_rh=0;
    json_t *jh = json_object_get(root, "humidity");
    if (json_is_number(jh)){ rh = json_number_value(jh); has_rh=1; }
    if (!has_rh){
        jh = json_object_get(root, "humidity_pct");
        if (json_is_number(jh)){ rh = json_number_value(jh); has_rh=1; }
    }

    // normalize id/location
    char id_buf[MAX_ID_LEN]; char loc_buf[MAX_LOC_LEN];
    snprintf(id_buf,  sizeof(id_buf),  "%s", id);
    snprintf(loc_buf, sizeof(loc_buf), "%s", location);
    trim_inplace(id_buf);
    trim_inplace(loc_buf);

    if (!excluded_location(loc_buf)){
        int idx = find_device(id_buf, loc_buf);
        if (idx < 0) idx = add_device(id_buf, loc_buf);
        if (idx >= 0){
            devices[idx].ts = ts;
            if (has_temp){ devices[idx].temp = temp; devices[idx].has_temp=1; }
            if (has_rh){ devices[idx].rh = rh; devices[idx].has_rh=1; }
        }
    }

    json_decref(root);
}

// -------- averages ----------
static void compute_averages(double *in_t, double *in_h, int *in_n,
                             double *g_t,  double *g_h,  int *g_n){
    long long cutoff = now_i() - g_stale_secs;

    double sum_in_t=0, sum_in_h=0; int nin_t=0, nin_h=0, cin=0;
    double sum_g_t=0, sum_g_h=0;   int ng_t=0,  ng_h=0,  cg=0;

    for (int i=0;i<device_count;i++){
        Device *d = &devices[i];
        if (d->ts < cutoff) continue; // only fresh for averages
        int is_garden = equals_ci(d->location, "garden");
        if (is_garden){
            if (d->has_temp){ sum_g_t += d->temp; ng_t++; }
            if (d->has_rh){   sum_g_h += d->rh;   ng_h++; }
            if (d->has_temp || d->has_rh) cg++;
        } else {
            if (d->has_temp){ sum_in_t += d->temp; nin_t++; }
            if (d->has_rh){   sum_in_h += d->rh;   nin_h++; }
            if (d->has_temp || d->has_rh) cin++;
        }
    }

    *in_t = nin_t? (sum_in_t/nin_t) : NAN;
    *in_h = nin_h? (sum_in_h/nin_h) : NAN;
    *g_t  = ng_t?  (sum_g_t/ng_t)   : NAN;
    *g_h  = ng_h?  (sum_g_h/ng_h)   : NAN;
    if (in_n) *in_n = cin;
    if (g_n)  *g_n  = cg;
}

// -------- drawing helpers ----------
static void safe_addstr_xy(int y, int x, const char *s, attr_t attr){
    int H, W; getmaxyx(stdscr, H, W);
    if (y<0 || y>=H || x>=W || !s) return;
    if (x<0){ s += (-x); x = 0; }
    int maxlen = W - x;
    if (maxlen <= 0) return;
    attron(attr);
    mvaddnstr(y, x, s, maxlen);
    attroff(attr);
}
static void draw_box(int y, int x, int h, int w, const char *title){
    int H, W; getmaxyx(stdscr, H, W);
    if (y>=H || x>=W) return;
    h = imax(PANEL_MIN_H, imin(h, H - y));
    w = imax(4, imin(w, W - x));
    if (h < PANEL_MIN_H || w < 4) return;

    for (int i=x;i<x+w;i++){
        mvaddch(y, i, ACS_HLINE);
        mvaddch(y+h-1, i, ACS_HLINE);
    }
    for (int j=y;j<y+h;j++){
        mvaddch(j, x, ACS_VLINE);
        mvaddch(j, x+w-1, ACS_VLINE);
    }
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x+w-1, ACS_URCORNER);
    mvaddch(y+h-1, x, ACS_LLCORNER);
    mvaddch(y+h-1, x+w-1, ACS_LRCORNER);

    char buf[128]; snprintf(buf, sizeof(buf), " %s ", title);
    safe_addstr_xy(y, x+2, buf, A_BOLD);
}

// -------- table ----------
typedef struct { const Device *ptr; } RowRef;
static int cmp_rowref_by_id(const void *a, const void *b){
    const RowRef *ra = (const RowRef*)a, *rb = (const RowRef*)b;
    int c = strcmp(ra->ptr->id, rb->ptr->id);
    if (c) return c;
    return strcmp(ra->ptr->location, rb->ptr->location);
}

static void draw_averages_bar(int y, int x, int w, const char *title,
                              double t_avg, double h_avg, int cnt){
    draw_box(y, x, 3, w, title);
    char line[256];
    char tbuf[32], hbuf[32];
    if (is_finite(t_avg)) snprintf(tbuf, sizeof(tbuf), "%.1f°C", t_avg); else snprintf(tbuf, sizeof(tbuf), "--");
    if (is_finite(h_avg)) snprintf(hbuf, sizeof(hbuf), "%.0f%%", h_avg); else snprintf(hbuf, sizeof(hbuf), "--");
    snprintf(line, sizeof(line), " Temp: %s   RH: %s   Devices: %d (fresh ≤%ds) ",
             tbuf, hbuf, cnt, g_stale_secs);
    safe_addstr_xy(y+1, x+2, line, A_NORMAL);
}

static void draw_table(int y, int x, int h, int w, int scroll){
    draw_box(y, x, h, w, "Sensors (dew point shown per row)");
    int inner_top = y+1, inner_left = x+1;
    int inner_bottom = y+h-2, inner_right = x+w-2;
    int inner_h = imax(1, inner_bottom - inner_top + 1);
    int inner_w = imax(20, inner_right - inner_left + 1);

    struct { const char *name; int w; } cols[] = {
        {"Room", 14}, {"Device", 18}, {"Temp(°C)", 10}, {"RH(%)", 7}, {"DewPt(°C)", 11}, {"Age(s)", 8}
    };
    const int ncol = 6;
    int total = 0; for (int i=0;i<ncol;i++) total += cols[i].w; total += ncol-1;
    if (total > inner_w){
        int over = total - inner_w;
        int take_room = imin(over/2, imax(0, cols[0].w - 8));
        int take_dev  = imin(over - take_room, imax(0, cols[1].w - 10));
        cols[0].w -= take_room; cols[1].w -= take_dev;
        total = 0; for (int i=0;i<ncol;i++) total += cols[i].w; total += ncol-1;
    }

    int cx = inner_left;
    for (int i=0;i<ncol;i++){
        char hdr[32]; snprintf(hdr, sizeof(hdr), "%-*s", cols[i].w, cols[i].name);
        safe_addstr_xy(inner_top, cx, hdr, A_BOLD);
        cx += cols[i].w + 1;
    }

    // collect + sort (exclude attic)
    RowRef *rows = (RowRef*)malloc(sizeof(RowRef)*device_count);
    int rc=0;
    for (int i=0;i<device_count;i++){
        if (excluded_location(devices[i].location)) continue;
        rows[rc++].ptr = &devices[i];
    }
    qsort(rows, rc, sizeof(RowRef), cmp_rowref_by_id);

    int max_rows = inner_h - 1;
    int start = scroll;
    if (start < 0) start = 0;
    if (start > rc - max_rows) start = imax(0, rc - max_rows);

    long long cutoff = now_i() - g_stale_secs;

    int ry = inner_top + 1;
    for (int i=start; i<imin(start+max_rows, rc); i++){
        const Device *d = rows[i].ptr;
        long long age = imax(0, now_i() - d->ts);
        int stale = (d->ts < cutoff);
        int is_garden = equals_ci(d->location, "garden");

        char room[64], dev[128], tbuf[32], rbuf[32], dpbuf[32], abuf[32];
        snprintf(room, sizeof(room), "%-*.*s", cols[0].w, cols[0].w, d->location);
        snprintf(dev,  sizeof(dev),  "%-*.*s", cols[1].w, cols[1].w, d->id);

        if (d->has_temp) snprintf(tbuf, sizeof(tbuf), "%*.1f", cols[2].w, d->temp);
        else snprintf(tbuf, sizeof(tbuf), "%*s", cols[2].w, "-");

        if (d->has_rh) snprintf(rbuf, sizeof(rbuf), "%*.0f", cols[3].w, d->rh);
        else snprintf(rbuf, sizeof(rbuf), "%*s", cols[3].w, "-");

        double dp = (d->has_temp && d->has_rh)? dewpoint_c(d->temp, d->rh) : NAN;
        if (is_finite(dp)) snprintf(dpbuf, sizeof(dpbuf), "%*.1f", cols[4].w, dp);
        else snprintf(dpbuf, sizeof(dpbuf), "%*s", cols[4].w, "-");

        snprintf(abuf, sizeof(abuf), "%*lld", cols[5].w, age);

        int cx2 = inner_left;
        attr_t row_attr = stale ? A_DIM : A_NORMAL;
        // Room
        safe_addstr_xy(ry, cx2, room, row_attr); cx2 += cols[0].w + 1;
        // Device
        safe_addstr_xy(ry, cx2, dev,  row_attr); cx2 += cols[1].w + 1;
        // Temp
        safe_addstr_xy(ry, cx2, tbuf, row_attr); cx2 += cols[2].w + 1;
        // RH (highlight if indoor and out of [30..60])
        attr_t rh_attr = row_attr;
        int rh_alert = (!is_garden && d->has_rh && (d->rh < 30.0 || d->rh > 60.0));
        if (g_has_colors && rh_alert){
            rh_attr = (row_attr & ~A_DIM) | COLOR_PAIR(CP_WARN) | A_BOLD;
        }
        safe_addstr_xy(ry, cx2, rbuf, rh_attr); cx2 += cols[3].w + 1;
        // Dew point
        safe_addstr_xy(ry, cx2, dpbuf,row_attr); cx2 += cols[4].w + 1;
        // Age
        safe_addstr_xy(ry, cx2, abuf, row_attr);
        ry++;
    }

    char foot[160];
    snprintf(foot, sizeof(foot), " %d sensors • showing %d–%d • Indoor RH <30%% or >60%% highlighted ",
             rc, rc? start+1:0, imin(start+max_rows, rc));
    safe_addstr_xy(inner_bottom, imax(inner_left, inner_left + inner_w - (int)strlen(foot)), foot, A_DIM);

    free(rows);
}

// -------- main ----------
int main(void){
    const char *e = getenv("SB_STALE_SECS");
    if (e && *e){
        int v = atoi(e);
        if (v > 0 && v < 86400) g_stale_secs = v;
    }

    setlocale(LC_ALL, "");
    initscr(); cbreak(); noecho();
    keypad(stdscr, TRUE); nodelay(stdscr, TRUE); curs_set(0);

    // colors for RH alert
    if (has_colors()){
        start_color();
        g_has_colors = 1;
        init_pair(CP_WARN, COLOR_WHITE, COLOR_RED);
    }

    LineQueue *q = q_create(1024);
    Reader reader; reader_start(&reader, q);

    int scroll = 0;
    int running = 1;

    while (running){
        // drain input
        for (;;){
            char *line = q_pop_nowait(q);
            if (!line) break;
            process_json_line(line);
            free(line);
        }

        // compute averages (fresh only)
        double in_t, in_h, g_t, g_h; int in_n, g_n;
        compute_averages(&in_t, &in_h, &in_n, &g_t, &g_h, &g_n);

        // draw
        erase();
        int H, W; getmaxyx(stdscr, H, W);
        mvaddstr(0, 0, "SwitchBot Sensors (ncurses) — excluding 'attic'");
        attron(A_BOLD); mvaddstr(0, 0, "SwitchBot Sensors (ncurses) — excluding 'attic'"); attroff(A_BOLD);
        // footer
        mvaddstr(H-1, 0, "q quit • ↑/↓ or j/k to scroll • Averages include only fresh readings");

        // two small average panels on top
        int panel_w = imax(PANEL_MIN_W, (W - 3) / 2);
        draw_averages_bar(1, 0, panel_w, "Inside (location != 'garden')", in_t, in_h, in_n);
        draw_averages_bar(1, panel_w + 2, W - (panel_w + 2), "Garden (location == 'garden')", g_t, g_h, g_n);

        // table below
        int tbl_y = 1 + 3 + 1; // two panels (height 3) + gap
        int tbl_h = imax(TABLE_MIN_H, H - tbl_y - 1);
        draw_table(tbl_y, 0, tbl_h, W, scroll);

        // input
        int ch = getch();
        if (ch == 'q' || ch == 'Q') running = 0;
        else if (ch == KEY_DOWN || ch == 'j') scroll++;
        else if (ch == KEY_UP   || ch == 'k') scroll = imax(0, scroll-1);
        else if (ch == KEY_NPAGE) scroll += 10;
        else if (ch == KEY_PPAGE) scroll = imax(0, scroll-10);

        refresh();
        sleep_ms(FRAME_SLEEP_MS);
    }

    reader_stop(&reader);
    q_destroy(q);
    endwin();
    return 0;
}
