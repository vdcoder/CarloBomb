#include <stdio.h>

#define UNDECIDED -1
#define WHITE 0
#define BLACK 1
#define MAX_NODES 10
#define MAX_STACK 10000

static const char *target_str = "010010001";  /* 0=WHITE, 1=BLACK */
#define target(i) (target_str[i] - '0')
static int target_len = 9;

typedef struct { int color, links[2]; } Node;
typedef enum { CTRL, FLOW } Func;
typedef enum { INIT, LOOP_C, LOOP_C_R, LOOP_L, LOOP_L_R, TRY_N, TRY_N_R, MATCH, SIMPLE_R } State;
typedef struct { Func fn; int fp, cp; State st; int it, dir, nxt, set; } Frame;

static Node G[MAX_NODES];
static Frame S[MAX_STACK];
static int Gn, Sp, gen;

static void push(Func fn, int fp, int cp) {
    S[Sp++] = (Frame){fn, fp, cp, INIT, 0, 0, 0, 0};
}

static void print_sol(int max) {
    printf("\n=== SOLUTION (%d nodes) ===\n", Gn);
    for (int i = 0; i < Gn; i++) {
        char c = G[i].color == WHITE ? 'W' : G[i].color == BLACK ? 'B' : '?';
        printf("  %d: %c -> [", i, c);
        G[i].links[0] == UNDECIDED ? printf("?") : printf("%d", G[i].links[0]);
        printf(", ");
        G[i].links[1] == UNDECIDED ? printf("?") : printf("%d", G[i].links[1]);
        printf("]\n");
    }
}

static int solve(int max) {
    G[0] = (Node){UNDECIDED, {UNDECIDED, UNDECIDED}};
    Gn = 1; gen = 0; Sp = 0;
    push(CTRL, 0, 0);
    while (Sp > 0) {
        Frame *f = &S[Sp-1];
        int fp = f->fp, cp = f->cp;
        if (f->fn == CTRL) {
            switch (f->st) {
            case INIT: {
                int d = G[fp].color;
                if (d == UNDECIDED) { f->st = LOOP_C; f->it = 0; }
                else {
                    int nxt = G[cp].links[d];
                    if (nxt == UNDECIDED) { f->dir = d; f->st = LOOP_L; f->it = 0; }
                    else {
                        int cc = G[nxt].color;
                        if (cc == UNDECIDED) { G[nxt].color = target(gen); f->set = 1; }
                        else if (cc != target(gen)) { Sp--; break; }
                        else f->set = 0;
                        if (++gen == target_len) { print_sol(max); return 1; }
                        f->nxt = nxt; f->st = MATCH; push(FLOW, fp, nxt);
                    }
                }
                break;
            }
            case LOOP_C:
                if (f->it < 2) { G[fp].color = f->it++; f->st = LOOP_C_R; push(CTRL, fp, cp); }
                else Sp--;
                break;
            case LOOP_C_R: G[fp].color = UNDECIDED; f->st = LOOP_C; break;
            case LOOP_L:
                if (f->it < Gn) { G[cp].links[f->dir] = f->it++; f->st = LOOP_L_R; push(CTRL, fp, cp); }
                else { G[cp].links[f->dir] = UNDECIDED; f->st = TRY_N; }
                break;
            case LOOP_L_R: G[cp].links[f->dir] = UNDECIDED; f->st = LOOP_L; break;
            case TRY_N:
                if (Gn < max) {
                    G[cp].links[f->dir] = Gn;
                    G[Gn++] = (Node){UNDECIDED, {UNDECIDED, UNDECIDED}};
                    f->st = TRY_N_R; push(CTRL, fp, cp);
                } else Sp--;
                break;
            case TRY_N_R: Gn--; G[cp].links[f->dir] = UNDECIDED; Sp--; break;
            case MATCH:
                if (f->set) G[f->nxt].color = UNDECIDED;
                gen--; Sp--;
                break;
            default: break;
            }
        } else { /* FLOW */
            switch (f->st) {
            case INIT: {
                int d = G[cp].color;
                if (d == UNDECIDED) { f->st = LOOP_C; f->it = 0; }
                else {
                    int nxt = G[fp].links[d];
                    if (nxt == UNDECIDED) { f->dir = d; f->st = LOOP_L; f->it = 0; }
                    else { f->st = SIMPLE_R; push(CTRL, nxt, cp); }
                }
                break;
            }
            case LOOP_C:
                if (f->it < 2) { G[cp].color = f->it++; f->st = LOOP_C_R; push(FLOW, fp, cp); }
                else Sp--;
                break;
            case LOOP_C_R: G[cp].color = UNDECIDED; f->st = LOOP_C; break;
            case LOOP_L:
                if (f->it < Gn) { G[fp].links[f->dir] = f->it++; f->st = LOOP_L_R; push(FLOW, fp, cp); }
                else { G[fp].links[f->dir] = UNDECIDED; f->st = TRY_N; }
                break;
            case LOOP_L_R: G[fp].links[f->dir] = UNDECIDED; f->st = LOOP_L; break;
            case TRY_N:
                if (Gn < max) {
                    G[fp].links[f->dir] = Gn;
                    G[Gn++] = (Node){UNDECIDED, {UNDECIDED, UNDECIDED}};
                    f->st = TRY_N_R; push(FLOW, fp, cp);
                } else Sp--;
                break;
            case TRY_N_R: Gn--; G[fp].links[f->dir] = UNDECIDED; Sp--; break;
            case SIMPLE_R: Sp--; break;
            default: break;
            }
        }
    }
    return 0;
}

int main(void) {
    for (int n = 2; n <= MAX_NODES; n++)
        if (solve(n)) return 0;
    printf("No solution found.\n");
    return 1;
}
