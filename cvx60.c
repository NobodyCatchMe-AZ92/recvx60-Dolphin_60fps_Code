/*
 * CVX60 - true 60fps for Resident Evil CODE:Veronica X (GameCube PAL, GCDP08 rev0) in 60Hz mode.
 *
 * Game logic keeps ticking at 30Hz exactly as in vanilla. Every tick is shown as two frames:
 *   A) the tick's own draw, with camera and bone matrices interpolated halfway from the previous tick
 *   B) an extra draw-only pass with the exact matrices of this tick
 * The extra frame skips everything stateful in njWaitVSync (EOR callback, sound sync), so timers,
 * input and audio are untouched. Anything unusual on screen (menus, messages, mirrors, scopes,
 * door demos, FMVs, pause) falls back to the vanilla 30fps path.
 *
 * All addresses are PAL (GCDP08). Mutable state lives in .bss only, because the AR code rewrites
 * .text/.rodata every frame.
 */
typedef unsigned int u32;
typedef unsigned char u8;
typedef float f32;

/* ---- game globals ---- */
#include "regions.h"
#define R13 A_R13
#define G32(off) (*(volatile u32 *)(R13 - (off)))
#define G8(off) (*(volatile u8 *)(R13 - (off)))

#define SYSP (*(u8 **)(R13 - O_SYSP))
#define SYS32(o) (*(volatile u32 *)(SYSP + (o)))
#define SYSF(o) (*(volatile f32 *)(SYSP + (o)))

#define LAST_RETRACE G32(O_LAST_RETRACE)
#define VSYNC_INTERVAL G32(O_VSYNC_INTERVAL)
#define VSYNC_MODE G32(O_VSYNC_MODE)
#define DRAW_MODE G8(O_DRAW_MODE)
#define PL_SLEEP G8(O_PL_SLEEP)

#define CAM_BASE A_CAM_BASE
#define CAM_MTX (*(f32 **)(CAM_BASE + 0x9C))
#define CAM_NCUT (*(volatile int *)(CAM_BASE + 0x8C))
#define CAM_PERS (*(volatile int *)(CAM_BASE + 0x84))

/* ---- game functions ---- */
#define FN(addr, ret, ...) ((ret(*)(__VA_ARGS__))(addr))
#define njWaitVSync FN(A_njWaitVSync, void, void)
#define njSetPerspective FN(A_njSetPerspective, void, int)
#define bhSetLight FN(A_bhSetLight, void, void)
#define bhControlLight FN(A_bhControlLight, void, void)
#define bhAllDrawModel FN(A_bhAllDrawModel, void, void)
#define bhEtcMirrorDrawModel FN(A_bhEtcMirrorDrawModel, void, void)
#define bhAllEasyDrawModel FN(A_bhAllEasyDrawModel, void, void)
#define bhDrawScreenFade FN(A_bhDrawScreenFade, void, void)
#define bhDrawCinesco FN(A_bhDrawCinesco, void, void)
#define njSetBlendState FN(A_njSetBlendState, void, int, int, int)
#define SetCopyFilter FN(A_SetCopyFilter, void, int)
#define GetDrawXfb FN(A_GetDrawXfb, void *, void)
#define GXCopyDisp FN(A_GXCopyDisp, void, void *, int)
#define GXDrawDone FN(A_GXDrawDone, void, void)
#define TakeSkipFlip FN(A_TakeSkipFlip, int, int)
#define VISetNextFrameBuffer FN(A_VISetNextFrameBuffer, void, void *)
#define SwapXfb FN(A_SwapXfb, void, void)
#define CriServer FN(A_CriServer, void, void)
#define VIWaitForRetrace FN(A_VIWaitForRetrace, void, void)
#define VIGetRetraceCount FN(A_VIGetRetraceCount, u32, void)
#define VIFlush FN(A_VIFlush, void, void)

/* bhPutModel's first instruction, relocated (see asm at the bottom) */
extern void PutModelOrig(u8 *ewP);

/* ---- state (.bss) ---- */
#if defined(CVX_USA)
/* USA leaves only ~49KB above its heaps (0x817F34C0-0x817FF800): smaller pools; models that don't fit are
 * simply drawn exact. PLAYER_RESERVE keeps room for the player, who is drawn after the enemies. */
#define MAXENT 48
#define POOLMTX 200
#define EF_SAVE_MAX 128
#define TRACE_N 32
#define CAMLOG_N 32
#else
#define MAXENT 96
#define POOLMTX 640
#define EF_SAVE_MAX 256
#define TRACE_N 256
#define CAMLOG_N 128
#endif
#define PLAYER_RESERVE 40
typedef struct {
    u8 *key;      /* O_WORK array of the model */
    int n;        /* bone count */
    f32 *cur;     /* n * 12 floats in this tick's pool */
    f32 *prev;    /* n * 12 floats in last tick's pool, or 0 */
} Ent;

static struct {
    u32 tick;
    int interp;          /* this tick's draw is interpolated */
    int phase;           /* 1 while drawing the exact extra frame */
    int cam_ok;
    int in_a;
    u32 tb_tick, phase_a;  /* timebase at tick start (just after a retrace) / how far into its field frame A was copied */
    int cut_prev, nomodel; /* camera cut last tick / no model+effect blend this tick */            /* inside the frame-A draw-all call: bones may stay interpolated */
    f32 cam[2][12];
    u32 cam_tick[2];
    int cam_cut[2];
    int pers[2];
    u32 gm_draw; /* sys->gm_flg at draw time */
    f32 campos[3]; int camang[3]; /* cam.px/py/pz, ax/ay/az at draw time */
    Ent ent[2][MAXENT];
    int nent[2];
    int used[2];
    f32 pool[2][POOLMTX * 12];
    f32 scratch[64 * 12];
    f32 cam_dt, cam_dr;          /* camera motion of the previous tick (translation, rotation) */
} S;

#ifdef CVX_LAB
/* Test/diagnostic block, a separate symbol (cvx_D) so outside tools find it by name.
 * Field order is part of that interface: tools/cvxdbg.py. */
struct {
    u32 off;      /* non-zero: force vanilla 30fps path (A/B testing) */
    u32 dbg;      /* 1: leave the extra frame blank (presentation test) */
    u32 fake_on;  /* buttons OR'ed into sys->pad_on before gameplay logic (test harness) */
    u32 stat_extra, stat_late, stat_vanilla;
    u32 stat_camok, stat_camcut, stat_camfar; /* camera blend engaged / blocked by cut / by distance */
    u32 stat_bskip; /* extra frames skipped because post-draw logic cut the camera */
    u32 fake_ps;  /* buttons OR'ed into sys->pad_ps (pressed edge) - test harness */
    u32 why_last, why_or, act_last, act_or; /* interp_block() reasons and active tasks while blocked */
    u32 stat_poserej; /* models drawn exact because their pose jumped */
    u32 delay_us; /* lab: spin before the extra frame's copy (presentation-phase test) */
    u32 phase_last, phase_max; /* lab: frame A copy phase (timebase ticks) */
    f32 rej_d[16]; u32 rej_i; /* lab: max basis change of recent rejections */
    u32 rej_key[16]; f32 rej_dy[16], rej_dxz[16];
} cvx_D;

/* rings read from outside for verification (tools/cvxdbg.py): what each presented frame was drawn
 * with, and the per-tick camera blend decision */
u32 cvx_tr_i;
struct { u32 tick, phase, vi; f32 cam[3], pl[3]; } cvx_tr[TRACE_N];
u32 cvx_cl_i;
struct { u32 tick; int ncut; f32 dt, dr; u32 dec; int pers; } cvx_cl[CAMLOG_N];
#define STAT(f) (cvx_D.f++)
#define LAB_OFF (cvx_D.off)
#define LAB_BLANK (cvx_D.dbg == 1)
#define LAB_WHY(w, a) do { cvx_D.why_last = (w); cvx_D.why_or |= (w); if (w) { cvx_D.act_last = (a); cvx_D.act_or |= (a); } } while (0)
#define LAB_CAMLOG(tick_, ncut_, dt_, dr_, dec_, pers_) do { u32 i_ = ++cvx_cl_i & (CAMLOG_N - 1);     cvx_cl[i_].tick = (tick_); cvx_cl[i_].ncut = (ncut_); cvx_cl[i_].dt = (dt_); cvx_cl[i_].dr = (dr_);     cvx_cl[i_].dec = (dec_); cvx_cl[i_].pers = (pers_); } while (0)
#else
#define STAT(f) ((void)0)
#define LAB_OFF 0
#define LAB_BLANK 0
#define LAB_WHY(w, a) ((void)0)
#define LAB_CAMLOG(tick_, ncut_, dt_, dr_, dec_, pers_) ((void)0)
#endif

/* ---- savestates ----
 * A savestate holds this mod's RAM too. Loaded under a different CVX60 build, the state block has another
 * layout (garbage flags, stale pointers). Each build carries an ID; on mismatch all state is zeroed. */
#ifndef CVX_BUILD_ID
#define CVX_BUILD_ID 0x43565836u
#endif
extern u32 __bss_start[], __bss_end[];
u32 cvx_build_seen;
static inline void state_check(void) {
    if (cvx_build_seen == CVX_BUILD_ID) return;
    for (u32 *p = __bss_start; p < __bss_end; p++) *p = 0;
    cvx_build_seen = CVX_BUILD_ID;
}

/* ---- math ---- */
static inline f32 rsqrt(f32 x) {
    f32 y;
    __asm__("frsqrte %0,%1" : "=f"(y) : "f"(x));
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}

static inline f32 fabsf_(f32 x) { return x < 0 ? -x : x; }

/* halfway between two GX 3x4 matrices; basis columns renormalised to the mean length */
static void mtx_mid(f32 *d, const f32 *a, const f32 *b) {
    for (int i = 0; i < 12; i++) d[i] = 0.5f * (a[i] + b[i]);
    for (int c = 0; c < 3; c++) {
        f32 la = a[c] * a[c] + a[4 + c] * a[4 + c] + a[8 + c] * a[8 + c];
        f32 lb = b[c] * b[c] + b[4 + c] * b[4 + c] + b[8 + c] * b[8 + c];
        f32 ld = d[c] * d[c] + d[4 + c] * d[4 + c] + d[8 + c] * d[8 + c];
        if (la < 1e-12f || lb < 1e-12f || ld < 1e-12f) continue;
        f32 want = 0.5f * (la * rsqrt(la) + lb * rsqrt(lb));
        f32 k = want * rsqrt(ld);
        d[c] *= k; d[4 + c] *= k; d[8 + c] *= k;
    }
}

/* Halfway camera. A view matrix is [R | -R p] (p = camera position in the world). Averaging those
 * directly pulls the eye off its path whenever the camera turns, so the in-between frame steps
 * unevenly. Blend the eye position and the orientation separately, then rebuild. */
static void norm3(f32 *v) {
    f32 l = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
    if (l < 1e-20f) return;
    f32 k = rsqrt(l);
    v[0] *= k; v[1] *= k; v[2] *= k;
}

static void cam_mid(f32 *d, const f32 *a, const f32 *b) {
    f32 pa[3], pb[3], p[3], r[3][3];
    for (int j = 0; j < 3; j++) {
        pa[j] = -(a[j] * a[3] + a[4 + j] * a[7] + a[8 + j] * a[11]);
        pb[j] = -(b[j] * b[3] + b[4 + j] * b[7] + b[8 + j] * b[11]);
        p[j] = 0.5f * (pa[j] + pb[j]);
    }
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) r[i][j] = 0.5f * (a[i * 4 + j] + b[i * 4 + j]);
    /* re-orthonormalise rows (Gram-Schmidt), third row from the cross product, sign kept */
    norm3(r[0]);
    f32 dt = r[1][0] * r[0][0] + r[1][1] * r[0][1] + r[1][2] * r[0][2];
    for (int j = 0; j < 3; j++) r[1][j] -= dt * r[0][j];
    norm3(r[1]);
    f32 c[3] = {r[0][1] * r[1][2] - r[0][2] * r[1][1], r[0][2] * r[1][0] - r[0][0] * r[1][2],
                r[0][0] * r[1][1] - r[0][1] * r[1][0]};
    if (c[0] * r[2][0] + c[1] * r[2][1] + c[2] * r[2][2] < 0.0f) { c[0] = -c[0]; c[1] = -c[1]; c[2] = -c[2]; }
    for (int j = 0; j < 3; j++) r[2][j] = c[j];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) d[i * 4 + j] = r[i][j];
        d[i * 4 + 3] = -(r[i][0] * p[0] + r[i][1] * p[1] + r[i][2] * p[2]);
    }
}

/* rotation part close enough and translation within reach for a half-step blend */
static int mtx_close(const f32 *a, const f32 *b, f32 max_move) {
    f32 dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
    if (dx * dx + dy * dy + dz * dz > max_move * max_move) return 0;
    for (int i = 0; i < 12; i++) {
        if (i == 3 || i == 7 || i == 11) continue;
        if (fabsf_(a[i] - b[i]) > 0.9f) return 0; /* > ~50 deg change in one tick: a cut, not motion */
    }
    return 1;
}

/* every bone close: root within max_move, no basis element changing by more than ~35 degrees */
static int pose_close(const f32 *a, const f32 *b, int n, f32 max_move) {
    if (!mtx_close(a, b, max_move)) return 0;
    for (int k = 0; k < n * 12; k++) {
        int c = k % 12;
        if (c == 3 || c == 7 || c == 11) continue;
        if (fabsf_(a[k] - b[k]) > 0.6f) return 0;
    }
    return 1;
}

static void copy12(f32 *d, const f32 *s) {
    for (int i = 0; i < 12; i++) d[i] = s[i];
}

/* ---- when is it safe to interpolate? ---- */
#define TASKS_OK ((1u << 6) | (1u << 7) | (1u << 8) | (1u << 20) | (1u << 21) | (1u << 22))

/* Scripted scene: event-forced fixed camera (st_flg 0x1), event camera (cam.flg 0x2) or event lights
 * (st_flg 0x1000000). Scripts re-pose characters around cuts; normal gameplay never does. */
static int in_event(void) {
    return SYSP && ((SYS32(0x64) & 0x1000001u) || (*(volatile u32 *)CAM_BASE & 0x2u));
}

/* Why interpolation is off this tick (0 = it can run). Bits are reported by the lab build. */
static u32 interp_block(void) {
    u8 *s = SYSP;
    if (!s) return 1u << 0;
    u32 why = 0;
    u32 act = SYS32(0x58) & ~SYS32(0x5C);
    if (!(act & (1u << 7))) why |= 1u << 1;                                   /* gameplay task off */
    if (act & ~TASKS_OK) why |= 1u << 2;                                      /* other task running */
    if (SYS32(0x44) & 0x80000000u) why |= 1u << 3;                            /* pause */
    if (SYS32(0x64) & 0x100u) why |= 1u << 5;                                 /* mirror */
    if (SYS32(0x64) & (0x800000u | 0x40000000u)) why |= 1u << 6;              /* scope / thermo */
    if (SYS32(0x64) & 0x8u) why |= 1u << 7;                                   /* menu */
    /* gm_flg 0x200 = small render-to-texture (e.g. alarm-light rooms): drawn inside the tick before the
     * main draw, restores the camera itself; frame B just reuses this tick's texture. 0x100 = full-screen
     * render-to-texture: not analysed, stays 30. */
    if (SYS32(0x60) & 0x100u) why |= 1u << 8;
    if (SYS32(0x68) & 0x1u) why |= 1u << 9;                                   /* door demo */
    if (SYS32(0x1B060) != 1) why |= 1u << 10;                                 /* loop_ct */
    if (SYS32(0x1B168) != 0) why |= 1u << 11;                                 /* countdown */
    if (*(volatile unsigned short *)(SYSP + 0x2A550) != 0 || SYSF(0x2A564) > 0.0f) why |= 1u << 12; /* screensaver */
    if (SYS32(0x90) == 3) why |= 1u << 13;                                    /* battle-game HUD */
    if (DRAW_MODE > 2) why |= 1u << 14;
    if (VSYNC_INTERVAL != 2 || VSYNC_MODE == 0) why |= 1u << 15;
    if (LAB_OFF) why |= 1u << 31;
    LAB_WHY(why, act);
    return why;
}

static int can_interp(void) { return interp_block() == 0; }

/* ---- hook: bhMainSequence, between logic and draw (replaces bl bhControlLight) ---- */
void cvx_after_logic(void) {
    state_check();
    bhControlLight();
    S.tick++;
    S.interp = can_interp();
    int cur = S.tick & 1, prv = cur ^ 1;
    S.nent[cur] = 0;
    S.used[cur] = 0;
    f32 *cm = CAM_MTX;
    copy12(S.cam[cur], cm);
    S.cam_tick[cur] = S.tick;
    S.cam_cut[cur] = CAM_NCUT;
    S.pers[cur] = CAM_PERS;
    S.gm_draw = SYSP ? SYS32(0x60) : 0;
    for (int k = 0; k < 3; k++) {
        S.campos[k] = *(volatile f32 *)(CAM_BASE + 0x0C + 4 * k);
        S.camang[k] = *(volatile int *)(CAM_BASE + 0x54 + 4 * k);
    }
    S.cam_ok = 0;
    u32 dec = 0;
    if (S.cam_tick[prv] == S.tick - 1) {
        /* A hard cut is a jump far beyond the camera's recent motion. Zone changes (ncut) during a
         * continuous swing are not cuts, so ncut alone must not block the blend. */
        const f32 *a = S.cam[prv], *b = S.cam[cur];
        f32 dx = a[3] - b[3], dy = a[7] - b[7], dz = a[11] - b[11];
        f32 dt2 = dx * dx + dy * dy + dz * dz;
        f32 dt = dt2 > 0.0f ? dt2 * rsqrt(dt2) : 0.0f, dr = 0.0f;
        for (int i = 0; i < 12; i++) {
            if (i == 3 || i == 7 || i == 11) continue;
            f32 d = fabsf_(a[i] - b[i]);
            if (d > dr) dr = d;
        }
        /* Swings ease in and out (per-tick steps measured 0, 3.4, 6.4, 8.5, 9.5 ... 1.9 units walking,
         * more running), so compare against the previous step with generous floors. A zone change
         * (ncut) only counts as a cut when the camera also jumps on that same tick. */
        f32 lim_t = 3.0f * S.cam_dt, lim_r = 3.0f * S.cam_dr;
        if (lim_t < 6.0f) lim_t = 6.0f;
        if (lim_r < 0.3f) lim_r = 0.3f;
        int zone = S.cam_cut[prv] != S.cam_cut[cur];
        int cut = dt > lim_t || dr > lim_r || dr > 0.9f || (zone && (dt > 1.5f || dr > 0.12f));
        if (S.interp) {
            if (cut) { dec = zone ? 1 : 2; if (dec == 1) STAT(stat_camcut); else STAT(stat_camfar); }
            else { S.cam_ok = 1; STAT(stat_camok); dec = 3; }
        }
        /* a cut resets the motion history, so the next tick's limits use the floors */
        S.cam_dt = dt;
        S.cam_dr = dr;
        /* scripted scenes often re-pose characters on the tick after a cut: keep models and effects
         * exact on the cut tick and the next one */
        S.nomodel = in_event() && (cut || S.cut_prev);
        S.cut_prev = cut;
        LAB_CAMLOG(S.tick, S.cam_cut[cur], dt, dr, dec, S.pers[cur]);
    } else {
        S.cam_dt = S.cam_dr = 0.0f;
        S.nomodel = in_event();
        S.cut_prev = 1;
    }
}

/* ---- hooks: the three draw-all calls in bhMainSequence ---- */
#define PLP (*(u8 **)(R13 - O_PLP))
#ifdef CVX_LAB
static void trace_begin(int phase) {
    u32 i = ++cvx_tr_i & (TRACE_N - 1);
    f32 *cm = CAM_MTX;
    cvx_tr[i].tick = S.tick;
    cvx_tr[i].phase = phase;
    cvx_tr[i].vi = VIGetRetraceCount();
    cvx_tr[i].cam[0] = cm[3]; cvx_tr[i].cam[1] = cm[7]; cvx_tr[i].cam[2] = cm[11];
    cvx_tr[i].pl[0] = cvx_tr[i].pl[1] = cvx_tr[i].pl[2] = 0.0f;
}
static void trace_player(u8 *ewP, u8 *ow) {
    if (ewP != PLP) return;
    f32 *m = (f32 *)(ow + 0x10);
    u32 i = cvx_tr_i & (TRACE_N - 1);
    cvx_tr[i].pl[0] = m[3]; cvx_tr[i].pl[1] = m[7]; cvx_tr[i].pl[2] = m[11];
}
#else
#define trace_begin(p) ((void)0)
#define trace_player(e, o) ((void)0)
#endif

/* Effects (O_WRK, 0x4D0 bytes each) are queued during logic into per-kind lists on sys and
 * drawn later. Shadows (ef_mdf) and the 3D kinds build their transform at draw time from the effect's own
 * px/py/pz (+0x10) and ax/ay/az (+0x1C), or from the owner's bone if attached. bhControlEffect saves last
 * tick's values at +0x40..+0x54 before updating, so frame A can draw every effect at the midpoint.
 * GC list layout = PS2 SYS_WORK + 4: {count offset, list offset, capacity}. */
static const u32 EF_LISTS[][3] = {
    {0x222C0, 0x22AEC, 0x50},  /* ef_mdf: shadows */
    {0x222C8, 0x2342C, 512},   /* ef_ntx: 3D, no texture blend */
    {0x222CC, 0x23C2C, 512},   /* ef_trs: 3D translucent */
    {0x222D0, 0x2442C, 512},   /* ef_pnc: 3D punch-through */
    {0x222D4, 0x24C2C, 512},   /* ef_opq: 3D opaque */
    {0x222D8, 0x2542C, 512},   /* ef_thl: 3D */
};
static struct { u8 *op; f32 p[3]; int a[3]; } ef_save[EF_SAVE_MAX];
static int ef_n;

static void eff_mid(void) {
    ef_n = 0;
    if (S.nomodel) return;
    for (u32 l = 0; l < sizeof(EF_LISTS) / sizeof(EF_LISTS[0]); l++) {
        int n = (int)SYS32(EF_LISTS[l][0]);
        if (n > (int)EF_LISTS[l][2]) n = (int)EF_LISTS[l][2];
        u8 **list = (u8 **)(SYSP + EF_LISTS[l][1]);
        for (int i = 0; i < n && ef_n < EF_SAVE_MAX; i++) {
            u8 *op = list[i];
            if (!op) continue;
            f32 *cur = (f32 *)(op + 0x10), *old = (f32 *)(op + 0x40);
            int *ang = (int *)(op + 0x1C), *ang_old = (int *)(op + 0x4C);
            f32 dx = cur[0] - old[0], dy = cur[1] - old[1], dz = cur[2] - old[2];
            if (dx * dx + dy * dy + dz * dz > 3.0f * 3.0f) continue;   /* spawned/teleported: exact */
            ef_save[ef_n].op = op;
            for (int k = 0; k < 3; k++) { ef_save[ef_n].p[k] = cur[k]; ef_save[ef_n].a[k] = ang[k]; }
            ef_n++;
            for (int k = 0; k < 3; k++) {
                cur[k] = 0.5f * (cur[k] + old[k]);
                int da = (short)((ang[k] - ang_old[k]) & 0xFFFF);           /* shortest way round */
                ang[k] = ang_old[k] + da / 2;
            }
        }
    }
}

/* reverse order, so an effect queued in two lists ends up with its true values */
static void eff_restore(void) {
    while (ef_n > 0) {
        ef_n--;
        u8 *op = ef_save[ef_n].op;
        for (int k = 0; k < 3; k++) {
            ((f32 *)(op + 0x10))[k] = ef_save[ef_n].p[k];
            ((int *)(op + 0x1C))[k] = ef_save[ef_n].a[k];
        }
    }
}

/* Bones stay at their in-between pose for the whole frame-A draw (attached effects such as muzzle
 * flashes and laser sights read the owner's bone after the owner was drawn); put back here. */
static void bones_restore(void) {
    int cur = S.tick & 1;
    for (int i = 0; i < S.nent[cur]; i++) {
        Ent *e = &S.ent[cur][i];
        if (!e->prev) continue;
        for (int b = 0; b < e->n; b++) copy12((f32 *)(e->key + b * 0x40 + 0x10), e->cur + b * 12);
    }
}

static void with_mid_camera(void (*draw)(void)) {
    state_check();
    if (!S.interp || S.phase || !S.cam_ok) {
        if (S.interp) trace_begin(2);
        if (S.interp && !S.phase) { eff_mid(); S.in_a = 1; }
        draw();
        S.in_a = 0;
        eff_restore();
        if (S.interp && !S.phase) bones_restore();
        return;
    }
    int cur = S.tick & 1;
    f32 *cm = CAM_MTX;
    cam_mid(cm, S.cam[cur ^ 1], S.cam[cur]);
    int fov = S.pers[cur ^ 1] != S.pers[cur];
    if (fov) njSetPerspective((S.pers[cur ^ 1] + S.pers[cur]) / 2);
    /* lights live in view space (bhSetLight transforms them by cam.mtx); the tick's logic set them for
     * the end-of-tick camera, so re-apply them for the in-between camera or shading lags at 30Hz */
    bhSetLight();
    trace_begin(0);
    eff_mid();
    S.in_a = 1;
    draw();
    S.in_a = 0;
    eff_restore();
    bones_restore();
    copy12(cm, S.cam[cur]);
    if (fov) njSetPerspective(S.pers[cur]);
}
void cvx_alldraw(void) { with_mid_camera(bhAllDrawModel); }
void cvx_mirrordraw(void) { with_mid_camera(bhEtcMirrorDrawModel); }
void cvx_easydraw(void) { with_mid_camera(bhAllEasyDrawModel); }

#ifdef CVX_LAB
/* ---- hook: task table slot 7 (bhSysCallGame) - test harness input ---- */
#define bhSysCallGame FN(A_bhSysCallGame, void, u32)
void cvx_syscallgame(u32 a) {
    state_check();
    if (cvx_D.fake_on && SYSP) SYS32(0x1B0B4) |= cvx_D.fake_on;
    if (cvx_D.fake_ps && SYSP) SYS32(0x1B0BC) |= cvx_D.fake_ps;
    bhSysCallGame(a);
}
#endif

/* ---- hook: bhPutModel entry ---- */
static Ent *lookup(int pool, u8 *key, int n) {
    for (int i = 0; i < S.nent[pool]; i++)
        if (S.ent[pool][i].key == key && S.ent[pool][i].n == n) return &S.ent[pool][i];
    return 0;
}

void cvx_putmodel(u8 *ewP) {
    state_check();
    if (!S.interp && !S.phase) {
        PutModelOrig(ewP);
        return;
    }
    u32 *ml = *(u32 **)(ewP + 0x2E0);
    int n = ml ? (int)ml[1] : 0;
    u8 *ow = ml ? (u8 *)ml[5] : 0;
    if (!ow || n <= 0 || n > 64) {
        PutModelOrig(ewP);
        return;
    }
    int cur = S.tick & 1;
    Ent *e = lookup(cur, ow, n);
    if (S.phase) {
        /* exact frame: show the bones as they were at this tick's draw, not as post-draw logic
         * (cuts, events) may have left them */
        if (!e) {
            PutModelOrig(ewP);
            return;
        }
        for (int i = 0; i < n; i++) {
            f32 *m = (f32 *)(ow + i * 0x40 + 0x10);
            copy12(S.scratch + i * 12, m);
            copy12(m, e->cur + i * 12);
        }
        trace_player(ewP, ow);
        PutModelOrig(ewP);
        for (int i = 0; i < n; i++) copy12((f32 *)(ow + i * 0x40 + 0x10), S.scratch + i * 12);
        return;
    }
    if (!e) {
        if (S.nent[cur] >= MAXENT || S.used[cur] + n > POOLMTX ||
            (ewP != PLP && S.used[cur] + n > POOLMTX - PLAYER_RESERVE)) {
            PutModelOrig(ewP);
            return;
        }
        e = &S.ent[cur][S.nent[cur]++];
        e->key = ow;
        e->n = n;
        e->cur = &S.pool[cur][S.used[cur] * 12];
        S.used[cur] += n;
        for (int i = 0; i < n; i++) copy12(e->cur + i * 12, (f32 *)(ow + i * 0x40 + 0x10));
        Ent *p = lookup(cur ^ 1, ow, n);
        e->prev = 0;
        /* previous pool is only valid if it was filled last tick */
        if (p && !S.nomodel && S.cam_tick[cur ^ 1] == S.tick - 1 &&
            (in_event() ? pose_close(p->cur, e->cur, n, 3.0f) : mtx_close(p->cur, e->cur, 3.0f))) e->prev = p->cur;
        else if (p && !S.nomodel && S.cam_tick[cur ^ 1] == S.tick - 1) {
            STAT(stat_poserej);
#ifdef CVX_LAB
            f32 md = 0.0f;
            for (int k = 0; k < n * 12; k++) { int c = k % 12; if (c == 3 || c == 7 || c == 11) continue;
                f32 d = fabsf_(p->cur[k] - e->cur[k]); if (d > md) md = d; }
            f32 dx = p->cur[3] - e->cur[3], dz = p->cur[11] - e->cur[11];
            f32 dy = p->cur[7] - e->cur[7];
            u32 ri = cvx_D.rej_i++ & 15;
            cvx_D.rej_d[ri] = md + 100.0f * (dx * dx + dz * dz > 4.0f);
            cvx_D.rej_key[ri] = (u32)ewP; cvx_D.rej_dy[ri] = dy; cvx_D.rej_dxz[ri] = dx * dx + dz * dz;
#endif
        }
    }
    if (!e->prev) {
        trace_player(ewP, ow);
        PutModelOrig(ewP);
        return;
    }
    for (int i = 0; i < n; i++) mtx_mid((f32 *)(ow + i * 0x40 + 0x10), e->prev + i * 12, e->cur + i * 12);
    trace_player(ewP, ow);
    PutModelOrig(ewP);
    /* inside frame A the pose stays in-between until bones_restore(), so attached effects drawn later
     * follow it; any other caller gets the exact pose back immediately */
    if (!S.in_a)
        for (int i = 0; i < n; i++) copy12((f32 *)(ow + i * 0x40 + 0x10), e->cur + i * 12);
}


/* ---- door transitions ----
 * bhSysCallDoordemo (task 11) runs bhControlDoor once per tick: logic procs (camera path, door leaf, lights,
 * fade), then a self-contained draw of the door scene. With DoorWrk.status bit 0x80 the procs are skipped
 * but the draw still runs, and its sound triggers are one-shot, so the scene can be redrawn safely with
 * an in-between camera and door pose. */
#define DOOR_STATUS (*(volatile u32 *)(A_DOOR_WRK + 0x08))
#define DOOR_VPOS ((volatile f32 *)(A_DOOR_WRK + 0x58))
#define DOOR_VANG ((volatile int *)(A_DOOR_WRK + 0x70))
#define DOOR_OBJP (*(u8 **)(A_DOOR_WRK + 0xC4))
#define bhControlDoor FN(A_bhControlDoor, int, void)
#define DOOR_MAXN 32
static struct {
    u32 n;                     /* door ticks seen */
    int pending;               /* this tick drew the door scene */
    u32 slot_n[2];
    u8 *objp[2];
    int nn[2];
    u8 *node[2][DOOR_MAXN];
    f32 vpos[2][3];
    int vang[2][3];
    f32 npos[2][DOOR_MAXN][3];
    int nang[2][DOOR_MAXN][3];
} DR;

static int door_nodes(u8 *root, u8 **out) {
    u8 *stack[DOOR_MAXN];
    int sp = 0, n = 0;
    if (root) stack[sp++] = root;
    while (sp && n < DOOR_MAXN) {
        u8 *o = stack[--sp];
        out[n++] = o;
        u8 *sib = *(u8 **)(o + 0x30), *child = *(u8 **)(o + 0x2C);
        if (sib && sp < DOOR_MAXN) stack[sp++] = sib;
        if (child && sp < DOOR_MAXN) stack[sp++] = child;
    }
    return n;
}

static void door_snapshot(void) {
    u32 st = DOOR_STATUS;
    if (!(st & 0x200) || (st & 0x40) || !DOOR_OBJP) return;
    int k = ++DR.n & 1;
    DR.slot_n[k] = DR.n;
    DR.objp[k] = DOOR_OBJP;
    DR.nn[k] = door_nodes(DOOR_OBJP, DR.node[k]);
    for (int j = 0; j < 3; j++) { DR.vpos[k][j] = DOOR_VPOS[j]; DR.vang[k][j] = DOOR_VANG[j]; }
    for (int i = 0; i < DR.nn[k]; i++)
        for (int j = 0; j < 3; j++) {
            DR.npos[k][i][j] = ((f32 *)(DR.node[k][i] + 0x08))[j];
            DR.nang[k][i][j] = ((int *)(DR.node[k][i] + 0x14))[j];
        }
    DR.pending = 1;
}

/* replaces the bl bhControlDoor in bhSysCallDoordemo */
int cvx_controldoor(void) {
    state_check();
    int r = bhControlDoor();
    door_snapshot();
    return r;
}

static int amid(int a, int b) { return a + (short)((b - a) & 0xFFFF) / 2; }

/* put the slot-k values back (or the midpoint between slots p and k) into the live door work */
static void door_apply(int k, int p, int mid) {
    for (int j = 0; j < 3; j++) {
        DOOR_VPOS[j] = mid ? 0.5f * (DR.vpos[p][j] + DR.vpos[k][j]) : DR.vpos[k][j];
        DOOR_VANG[j] = mid ? amid(DR.vang[p][j], DR.vang[k][j]) : DR.vang[k][j];
    }
    for (int i = 0; i < DR.nn[k]; i++)
        for (int j = 0; j < 3; j++) {
            ((f32 *)(DR.node[k][i] + 0x08))[j] = mid ? 0.5f * (DR.npos[p][i][j] + DR.npos[k][i][j]) : DR.npos[k][i][j];
            ((int *)(DR.node[k][i] + 0x14))[j] = mid ? amid(DR.nang[p][i][j], DR.nang[k][i][j]) : DR.nang[k][i][j];
        }
}

static void door_redraw(void) {
    u32 st = DOOR_STATUS;
    DOOR_STATUS = st | 0x80;   /* procs paused: draw only */
    bhControlDoor();
    DOOR_STATUS = st;
}

/* Message/subtitle window: bhControlMessage (event task) steps the text and draws it in one go. Redraw it
 * for the extra frame with its state saved and restored, and no buttons held so nothing can beep. */
#define bhControlMessage FN(A_bhControlMessage, void, int)
static void draw_message(void) {
    u8 *s = SYSP;
    if (!s || !(SYS32(0x64) & 0x200u)) return;
    static u32 flags[10], pad[8], mes[32];
    for (int i = 0; i < 10; i++) flags[i] = SYS32(0x58 + 4 * i);
    for (int i = 0; i < 8; i++) pad[i] = SYS32(0x1B0B4 + 4 * i);
    for (int i = 0; i < 32; i++) mes[i] = SYS32(0x28900 + 4 * i);
    for (int i = 0; i < 8; i++) SYS32(0x1B0B4 + 4 * i) = 0;
    bhControlMessage((SYS32(0x5C) & 0x200u) ? 0 : 1);
    for (int i = 0; i < 10; i++) SYS32(0x58 + 4 * i) = flags[i];
    for (int i = 0; i < 8; i++) SYS32(0x1B0B4 + 4 * i) = pad[i];
    for (int i = 0; i < 32; i++) SYS32(0x28900 + 4 * i) = mes[i];
}

/* ---- the exact extra frame ---- */
static void draw_only(void) {
    if (LAB_BLANK) return;
    u8 sleep = PL_SLEEP;
    /* the camera as it was at this tick's draw: the follow camera is updated after the draw
     * (bhCheckCut), and drawing with that newer matrix makes the camera step back and forth */
    f32 *cm = CAM_MTX;
    f32 live[12];
    copy12(live, cm);
    copy12(cm, S.cam[S.tick & 1]);
    int pers_live = CAM_PERS;
    njSetPerspective(S.pers[S.tick & 1]);
    /* the previous draw ended in bhSetHalfLight (character lighting); the room needs its own lights,
     * which the tick's logic re-applies before a normal draw */
    bhSetLight();
    trace_begin(1);
    switch (DRAW_MODE) {
    case 0: bhAllDrawModel(); break;
    case 1: bhEtcMirrorDrawModel(); break;
    case 2: bhAllEasyDrawModel(); break;
    }
    copy12(cm, live);
    njSetPerspective(pers_live);
    PL_SLEEP = sleep;
    draw_message();
    /* what bhSysCallScreenSaver draws on top (fade / cinemascope), without its logic */
    if (SYSF(0x2A520) > 0.0f) {
        njSetBlendState(1, 3, 1);
        bhDrawScreenFade();
    }
    if (SYSF(0x2A588) > 0.0f) bhDrawCinesco();
}

static void wait_until(u32 target) {
    while ((int)(VIGetRetraceCount() - target) < 0) {
        VIWaitForRetrace();
        CriServer();
    }
}

/* the GX half of njWaitVSync: copy EFB to the back XFB and queue it */
static inline u32 tbl(void) { u32 t; __asm__ volatile("mftb %0" : "=r"(t)); return t; }

/* Dolphin shows a frame from about when it is copied out, not only when the VI latches it. Frame A is copied
 * after the tick's logic and full draw, frame B right after a cheap redraw, so without care A stays up for
 * much less than B. Copy B at the same point of its field as A was copied in its own. */
#define TB_FIELD 675000u   /* 40.5 MHz timebase / 59.94 Hz */
static void present_extra(u32 target, u32 b_field_start) {
    u32 want = S.phase_a < TB_FIELD * 3 / 4 ? S.phase_a : TB_FIELD * 3 / 4;
#ifdef CVX_LAB
    if (cvx_D.delay_us) want = cvx_D.delay_us * 40;
#endif
    while (tbl() - b_field_start < want) {}
    SetCopyFilter(1);
    GXCopyDisp(GetDrawXfb(), 1);
    SetCopyFilter(0);
    GXDrawDone();
    if (TakeSkipFlip(0) == 0) {
        VISetNextFrameBuffer(GetDrawXfb());
        /* VISetNextFrameBuffer only stages the registers; Ninja's pre-retrace callback flushes them
         * solely at njWaitVSync's own target, so the extra frame has to flush itself. */
        VIFlush();
        SwapXfb();
    }
    CriServer();
    wait_until(target);
}

/* ---- hook: main loop (replaces bl njWaitVSync) ---- */
static int door_frame_end(void) {
    int k = DR.n & 1, p = k ^ 1;
    if (LAB_OFF || VSYNC_INTERVAL != 2 || VSYNC_MODE == 0) return 0;
    if (DR.slot_n[p] != DR.n - 1 || DR.objp[p] != DR.objp[k] || DR.nn[p] != DR.nn[k]) return 0;
    u32 st = DOOR_STATUS;
    if (!(st & 0x200) || (st & 0x40) || DOOR_OBJP != DR.objp[k]) return 0;
    /* jump inside the door sequence: show it exact in both frames */
    f32 dx = DR.vpos[k][0] - DR.vpos[p][0], dy = DR.vpos[k][1] - DR.vpos[p][1], dz = DR.vpos[k][2] - DR.vpos[p][2];
    int mid = dx * dx + dy * dy + dz * dz < 20.0f * 20.0f;
    for (int j = 0; j < 3; j++) {
        int da = (DR.vang[k][j] - DR.vang[p][j]) & 0xFFFF;
        if (da > 0x8000) da = 0x10000 - da;
        if (da > 0x2000) mid = 0;
    }
    u32 t0 = LAST_RETRACE;
    /* frame A: the tick already drew the exact scene; clear it and draw the in-between one */
    GXCopyDisp(GetDrawXfb(), 1);
    if (mid) door_apply(k, p, 1);
    door_redraw();
    door_apply(k, p, 0);
    S.phase_a = tbl() - S.tb_tick;
    VSYNC_INTERVAL = 1;
    njWaitVSync();
    VSYNC_INTERVAL = 2;
    u32 b_field = tbl();
    st = DOOR_STATUS;
    if ((st & 0x200) && !(st & 0x40) && (int)(VIGetRetraceCount() - (t0 + 2)) < 0) {
        S.phase = 1;
        door_redraw();             /* frame B: exact */
        present_extra(t0 + 2, b_field);
        S.phase = 0;
        STAT(stat_extra);
    } else {
        STAT(stat_late);
        wait_until(t0 + 2);
    }
    LAST_RETRACE = t0 + 2;
    return 1;
}

static void frame_end(void) {
    if (DR.pending) {
        DR.pending = 0;
        if (!S.interp && door_frame_end()) return;
    }
    if (!S.interp) {
        STAT(stat_vanilla);
        njWaitVSync();
        return;
    }
    S.interp = 0;
#ifdef CVX_LAB
    if (cvx_D.dbg == 3) { cvx_D.dbg = 1; LAST_RETRACE = LAST_RETRACE + 1; }   /* lab: shift field parity once */
#endif
    u32 t0 = LAST_RETRACE;
    S.phase_a = tbl() - S.tb_tick;   /* frame A is copied at the start of njWaitVSync */
#ifdef CVX_LAB
    cvx_D.phase_last = S.phase_a; if (S.phase_a > cvx_D.phase_max) cvx_D.phase_max = S.phase_a;
#endif
    VSYNC_INTERVAL = 1;
    njWaitVSync(); /* frame A at t0+1, runs EOR + sound sync once per tick as vanilla */
    VSYNC_INTERVAL = 2;
    u32 b_field = tbl();
    int cur = S.tick & 1;
    /* A cut applied after the draw (requested via gm_flg 0x1000/0x2000, or applied on the spot by
     * bhCheckCut -> bhSetCut, which sets gm_flg 0x20) swaps in the new shot's hidden geometry and
     * lights; redrawing the old view then shows voids or a wall in the lens. Follow-camera zone
     * handovers go through bhSetCut too but keep the camera continuous, so only skip the extra frame
     * when the camera was actually relocated. */
    int cut_applied = (S.gm_draw & 0x3000) || ((SYS32(0x60) & 0x20) && !(S.gm_draw & 0x20)) ||
                      CAM_NCUT != S.cam_cut[cur];
    int relocated = 0;
    if (cut_applied) {
        f32 d2 = 0.0f;
        for (int k = 0; k < 3; k++) {
            f32 d = *(volatile f32 *)(CAM_BASE + 0x0C + 4 * k) - S.campos[k];
            d2 += d * d;
            int da = (*(volatile int *)(CAM_BASE + 0x54 + 4 * k) - S.camang[k]) & 0xFFFF;
            if (da > 0x8000) da = 0x10000 - da;
            if (da > 0x800) relocated = 1;      /* > ~11 degrees in one tick */
        }
        if (d2 > 6.0f * 6.0f) relocated = 1;
    }
    int cut_after_draw = cut_applied && relocated;
    if (cut_after_draw) STAT(stat_bskip);
    if (!cut_after_draw && can_interp() && (int)(VIGetRetraceCount() - (t0 + 2)) < 0) {
        S.phase = 1;
        draw_only();
        present_extra(t0 + 2, b_field); /* frame B at t0+2 */
        S.phase = 0;
        STAT(stat_extra);
    } else {
        STAT(stat_late);
        wait_until(t0 + 2);
    }
    LAST_RETRACE = t0 + 2;
}

/* hook: main loop (replaces bl njWaitVSync). The next tick starts when this returns, just after a retrace:
 * remember the time so frame A's copy phase can be measured. */
void cvx_frame_end(void) {
    state_check();
    frame_end();
    S.tb_tick = tbl();
}

#define STR_(x) #x
#define XSTR(x) STR_(x)
/* relocated first instruction of bhPutModel, then back into it (A_bhPutModel + 4) */
__asm__(".section .text\n"
        ".global PutModelOrig\n"
        "PutModelOrig:\n"
        "  stwu 1,-0x30(1)\n"
        "  lis 12," XSTR(PUTMODEL_CONT_HI) "\n"
        "  ori 12,12," XSTR(PUTMODEL_CONT_LO) "\n"
        "  mtctr 12\n"
        "  bctr\n");
