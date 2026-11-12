/* `carlobomb bomb ...`: build, inspect and check a FixedRunBomb. */
#ifndef CARLOBOMB_FIXED_RUN_CLI_H
#define CARLOBOMB_FIXED_RUN_CLI_H

/* argv[1] is "bomb".  Returns the process exit status. */
int fixed_run_cli(int argc, char **argv);

/* The lines `carlobomb help` prints for the bomb commands. */
void fixed_run_cli_usage(void);

#endif
