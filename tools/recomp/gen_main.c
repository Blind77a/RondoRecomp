/* rondo_gen: developer front end for the code generator (Setup uses the same code).
 *
 * usage: rondo_gen <game.cue> --out DIR [--seeds FILE]... [--max-instrs N] [--verbose] [--progress]
 *   --progress: each step of its progress with the seconds it took to get there (to weigh
 *   the parts of the run: P_* in gen.c)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gen.h"

static void print_line(const char *s) { printf("%s\n", s); fflush(stdout); }

static clock_t started;
static void print_progress(int pct) { printf("progress %3d%% at %.2f s\n", pct, (double)(clock() - started) / CLOCKS_PER_SEC); }

int main(int argc, char **argv)
{
    GenOptions o = {0};
    int n_seeds = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--out") && i + 1 < argc) o.out_dir = argv[++i];
        else if (!strcmp(argv[i], "--seeds") && i + 1 < argc && n_seeds < 3) o.seed_files[n_seeds++] = argv[++i];
        else if (!strcmp(argv[i], "--max-instrs") && i + 1 < argc) o.max_region_instrs = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--verbose")) o.verbose = true;
        else if (!strcmp(argv[i], "--progress")) o.progress = print_progress;
        else o.cue = argv[i];
    }
    if (!o.cue || !o.out_dir) {
        fprintf(stderr, "usage: rondo_gen <game.cue> --out DIR [--seeds FILE]... [--max-instrs N] [--verbose] [--progress]\n");
        return 2;
    }
    o.build_id = "rondo_gen (developer build)";
    started = clock();
    GenStats st;
    char err[512];
    if (!gen_run(&o, &st, print_line, err, sizeof err)) {
        fprintf(stderr, "error: %s\n", err);
        return 1;
    }
    return 0;
}
